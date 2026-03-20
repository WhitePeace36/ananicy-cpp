#include "core/x3d.hpp"
#include "core/cpuset.hpp"
#include "utility/utils.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {

static const fs::path VCACHE_DRIVER_PATH =
    "/sys/bus/platform/drivers/amd_x3d_vcache";

/// Check if the amd_x3d_vcache driver has bound to an actual device.
/// The driver directory may exist (module loaded) even on non-X3D CPUs;
/// a bound device has an amd_x3d_mode sysfs entry.
bool is_x3d_driver_present() noexcept {
  return !x3d::find_driver_sysfs_path().empty();
}

/// Check /proc/cpuinfo for known AMD X3D model names
bool is_x3d_from_cpuinfo() noexcept {
  try {
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (!cpuinfo.is_open()) {
      return false;
    }

    std::string line;
    while (std::getline(cpuinfo, line)) {
      if (line.find("model name") == std::string::npos) {
        continue;
      }
      // Known X3D models
      if (line.find("X3D") != std::string::npos ||
          line.find("x3d") != std::string::npos) {
        return true;
      }
    }
  } catch (...) {
  }
  return false;
}

/// Read an integer from a sysfs file, returns -1 on failure
int read_sysfs_int(const fs::path &path) noexcept {
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return -1;
    }
    int val = -1;
    file >> val;
    return val;
  } catch (...) {
    return -1;
  }
}

/// Read a string from a sysfs file, trimmed
std::string read_sysfs_string(const fs::path &path) noexcept {
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return "";
    }
    std::string val;
    std::getline(file, val);
    // Trim trailing whitespace/newlines
    while (!val.empty() &&
           (val.back() == ' ' || val.back() == '\n' || val.back() == '\r')) {
      val.pop_back();
    }
    return val;
  } catch (...) {
    return "";
  }
}

/// Parse a cache size string like "98304K" or "32768K" to bytes
std::uint64_t parse_cache_size(const std::string &size_str) noexcept {
  if (size_str.empty()) {
    return 0;
  }
  try {
    std::size_t   pos = 0;
    std::uint64_t val = std::stoull(size_str, &pos);
    if (pos < size_str.size()) {
      char const suffix = size_str[pos];
      if (suffix == 'K' || suffix == 'k') {
        val *= 1024;
      } else if (suffix == 'M' || suffix == 'm') {
        val *= static_cast<std::uint64_t>(1024 * 1024);
      }
    }
    return val;
  } catch (...) {
    return 0;
  }
}

} // namespace

namespace x3d {

std::string find_driver_sysfs_path() noexcept {
  static const std::string cached = []() noexcept -> std::string {
    try {
      for (const auto &entry : fs::directory_iterator(VCACHE_DRIVER_PATH)) {
        if (!entry.is_directory()) {
          continue;
        }
        const auto name = entry.path().filename().string();
        // Skip standard sysfs entries
        if (name == "bind" || name == "unbind" || name == "uevent" ||
            name == "module") {
          continue;
        }

        auto mode_path = entry.path() / "amd_x3d_mode";
        if (fs::exists(mode_path)) {
          return mode_path.string();
        }
      }
    } catch (...) {
    }
    return "";
  }();
  return cached;
}

std::optional<X3DMode> get_driver_mode() noexcept {
  auto path = find_driver_sysfs_path();
  if (path.empty()) {
    return std::nullopt;
  }

  auto mode_str = read_sysfs_string(path);
  if (mode_str == "cache") {
    return X3DMode::cache;
  } else if (mode_str == "frequency") {
    return X3DMode::frequency;
  }

  spdlog::debug("x3d: unknown driver mode '{}'", mode_str);
  return std::nullopt;
}

bool set_driver_mode(X3DMode mode) noexcept {
  auto path = find_driver_sysfs_path();
  if (path.empty()) {
    spdlog::warn("x3d: driver sysfs path not found, cannot set mode");
    return false;
  }

  try {
    std::ofstream file(path);
    if (!file.is_open()) {
      spdlog::error("x3d: cannot open {} for writing", path);
      return false;
    }

    file << (mode == X3DMode::cache ? "cache" : "frequency");
    file.flush();

    if (file.fail()) {
      spdlog::error("x3d: failed to write mode to {}", path);
      return false;
    }

    spdlog::debug("x3d: set driver mode to {}",
                  mode == X3DMode::cache ? "cache" : "frequency");
    return true;
  } catch (const std::exception &e) {
    spdlog::error("x3d: exception setting driver mode: {}", e.what());
    return false;
  }
}

std::optional<X3DTopology> detect() noexcept {
  const bool driver_present = is_x3d_driver_present();
  const bool cpuinfo_match = is_x3d_from_cpuinfo();

  if (!driver_present && !cpuinfo_match) {
    spdlog::debug("x3d: no AMD X3D CPU detected");
    return std::nullopt;
  }

  spdlog::info("x3d: AMD X3D CPU detected (driver={}, cpuinfo={})",
               driver_present ? "yes" : "no", cpuinfo_match ? "yes" : "no");

  // Discover CCD topology by reading per-CPU die_id and L3 cache size
  const int num_cpus = static_cast<int>(cpuset::get_num_conf_cores());
  if (num_cpus <= 0) {
    spdlog::warn("x3d: could not determine number of CPUs");
    return std::nullopt;
  }

  // Map: die_id -> list of (cpu_id, l3_cache_size)
  std::map<int, std::vector<std::pair<int, std::uint64_t>>> die_map;

  for (int i = 0; i < num_cpus; ++i) {
    auto cpu_path = fmt::format("/sys/devices/system/cpu/cpu{}", i);

    // Read die_id (or cluster_id as fallback)
    int die_id = read_sysfs_int(fmt::format("{}/topology/die_id", cpu_path));
    if (die_id < 0) {
      die_id = read_sysfs_int(fmt::format("{}/topology/cluster_id", cpu_path));
    }
    if (die_id < 0) {
      // Fallback: use physical_package_id + core_id heuristic
      // If topology info is completely unavailable, we can't determine CCD
      spdlog::debug("x3d: no die_id for cpu{}, trying core_cpus_list", i);
      die_id = 0;
    }

    // Read L3 cache size
    auto cache_size_str =
        read_sysfs_string(fmt::format("{}/cache/index3/size", cpu_path));
    std::uint64_t l3_size = parse_cache_size(cache_size_str);

    die_map[die_id].emplace_back(i, l3_size);
  }

  if (die_map.size() < 2) {
    // Single CCD X3D (e.g., 7800X3D, 9800X3D)
    spdlog::info("x3d: single-CCD X3D detected, cpuset aliases map to all "
                 "cores");
    auto all_str = fmt::format("0-{}", num_cpus - 1);
    return X3DTopology{.cache_cores_str = all_str,
                       .frequency_cores_str = all_str,
                       .driver_present = driver_present};
  }

  // Multi-CCD: identify which die has the largest L3 (V-Cache CCD)
  int           vcache_die_id = -1;
  std::uint64_t max_l3 = 0;

  for (const auto &[die_id, cpus] : die_map) {
    std::uint64_t die_l3 = 0;
    for (const auto &[cpu_id, l3] : cpus) {
      if (l3 > 0) {
        die_l3 = l3;
        break;
      }
    }
    if (die_l3 > max_l3) {
      max_l3 = die_l3;
      vcache_die_id = die_id;
    }
  }

  if (vcache_die_id < 0) {
    spdlog::warn("x3d: could not determine V-Cache CCD from L3 sizes");
    return std::nullopt;
  }

  // Build cpuset strings for each CCD partition
  cpuset::CpuSet cache_set(num_cpus);
  cpuset::CpuSet freq_set(num_cpus);
  if (!cache_set.valid() || !freq_set.valid()) {
    return std::nullopt;
  }

  for (const auto &[die_id, cpus] : die_map) {
    for (const auto &[cpu_id, l3] : cpus) {
      if (die_id == vcache_die_id) {
        cache_set.set_cpu(cpu_id);
      } else {
        freq_set.set_cpu(cpu_id);
      }
    }
  }

  auto cache_str = cpuset::cpuset_to_string(cache_set);
  auto freq_str = cpuset::cpuset_to_string(freq_set);

  spdlog::info("x3d: V-Cache CCD (die {}) cores: {}", vcache_die_id, cache_str);
  spdlog::info("x3d: Frequency CCD cores: {}", freq_str);

  return X3DTopology{.cache_cores_str = std::move(cache_str),
                     .frequency_cores_str = std::move(freq_str),
                     .driver_present = driver_present};
}

} // namespace x3d
