#include "core/x3d.hpp"
#include "core/cpuset.hpp"
#include "utility/sysfs.hpp"

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

const fs::path VCACHE_DRIVER_PATH = "/sys/bus/platform/drivers/amd_x3d_vcache";

/// Check /proc/cpuinfo for known AMD X3D model names
auto is_x3d_from_cpuinfo() noexcept -> bool {
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

} // namespace

namespace x3d {

auto find_driver_sysfs_path() noexcept -> std::string {
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

auto get_driver_mode() noexcept -> std::optional<X3DMode> {
  const auto path = find_driver_sysfs_path();
  if (path.empty()) {
    return std::nullopt;
  }

  auto mode_str = sysfs::read_string(path);
  if (mode_str == "cache") {
    return X3DMode::cache;
  } else if (mode_str == "frequency") {
    return X3DMode::frequency;
  }

  spdlog::debug("x3d: unknown driver mode '{}'", mode_str);
  return std::nullopt;
}

auto set_driver_mode(X3DMode mode) noexcept -> bool {
  const auto path = find_driver_sysfs_path();
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

auto detect() noexcept -> std::optional<X3DTopology> {
  const bool driver_present = !find_driver_sysfs_path().empty();
  const bool cpuinfo_match = is_x3d_from_cpuinfo();

  if (!driver_present && !cpuinfo_match) {
    spdlog::debug("x3d: no AMD X3D CPU detected");
    return std::nullopt;
  }

  spdlog::info("x3d: AMD X3D CPU detected (driver={}, cpuinfo={})",
               driver_present ? "yes" : "no", cpuinfo_match ? "yes" : "no");

  // Discover CCD topology by reading per-CPU die_id and L3 cache size
  const auto num_cpus = static_cast<std::int32_t>(cpuset::get_num_conf_cores());
  if (num_cpus <= 0) {
    spdlog::warn("x3d: could not determine number of CPUs");
    return std::nullopt;
  }

  // Map: die_id -> list of (cpu_id, l3_cache_size)
  std::map<std::int32_t, std::vector<std::pair<std::int32_t, std::size_t>>>
      die_map;

  for (std::int32_t i = 0; i < num_cpus; ++i) {
    // Read die_id (or cluster_id as fallback)
    std::int32_t die_id =
        sysfs::read_int(sysfs::cpu_subpath(i, "topology/die_id"));
    if (die_id < 0) {
      die_id = sysfs::read_int(sysfs::cpu_subpath(i, "topology/cluster_id"));
    }
    if (die_id < 0) {
      spdlog::debug("x3d: no die_id for cpu{}, falling back to 0", i);
      die_id = 0;
    }

    die_map[die_id].emplace_back(i, sysfs::read_cpu_l3_size(i));
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
  std::int32_t vcache_die_id = -1;
  std::size_t  max_l3 = 0;

  for (const auto &[die_id, cpus] : die_map) {
    std::size_t die_l3 = 0;
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
