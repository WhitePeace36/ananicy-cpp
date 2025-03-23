#include "core/cgroups.hpp"

#include <filesystem>  // for path
#include <fstream>     // for ifstream
#include <mutex>       // for mutex, lock_guard
#include <optional>    // for optional
#include <string>      // for string
#include <string_view> // for string_view
#include <thread>      // for hardware_concurrency

#include <fmt/format.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

static fs::path get_cgroup_path(const std::string_view &cgroup_name) {
  const auto &cgroup_info = control_groups::get_cgroup_version();
  const auto &base_path = cgroup_info.path;

  spdlog::trace(fmt::format("{}: .path = {}, .version = {}", __func__,
                            std::string_view{cgroup_info.path.c_str()},
                            static_cast<int>(cgroup_info.version)));

  fs::path cgroup_path{base_path};
  switch (cgroup_info.version) {
  case control_groups::cgroup_info::none:
    break;
  case control_groups::cgroup_info::v1:
    cgroup_path /= fs::path{"cpu"} / cgroup_name.data();
    spdlog::trace(fmt::format("{}: cgroup_path: {}", __func__,
                              std::string_view{cgroup_path.c_str()}));
    break;
  case control_groups::cgroup_info::v2:
    cgroup_path /= cgroup_name;
    break;
  }
  return cgroup_path;
}

std::int32_t
control_groups::create_cgroup(const std::string_view &cgroup_name) {
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::warn(
        "Cgroups are not available on this platform (or are not enabled)");
    break;
  case cgroup_info::v1: {
    const auto &cgroup_path = get_cgroup_path(cgroup_name);
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup v1 {:s} already exists, ignoring.", cgroup_name);
      return false;
    }
    spdlog::debug(fmt::format("Creating cgroup (v1) {:s} at {:s}", cgroup_name,
                              std::string_view{cgroup_path.c_str()}));

    return create_directory(cgroup_path);
  }
  case cgroup_info::v2: {
    const auto &cgroup_path = get_cgroup_path(cgroup_name);
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup {:s} already exists, ignoring.", cgroup_name);
      return false;
    }
    spdlog::debug(fmt::format("Creating cgroup {:s} at {:s}", cgroup_name,
                              std::string_view{cgroup_path.c_str()}));

    return create_directory(cgroup_path);
  }
  }
  return false;
}

std::int32_t control_groups::set_cgroup_cpu_quota(const std::string &name,
                                                  std::uint32_t      quota) {
  std::int32_t errors = 0;
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::trace("{}: Skipping because no cgroup available", __func__);
    errors = 1;
    break;
  case cgroup_info::v1:
    try {
      const auto &cpu_quota_path = get_cgroup_path(name) / "cpu.cfs_quota_us";
      const auto &cpu_period_path = get_cgroup_path(name) / "cpu.cfs_period_us";

      const std::uint32_t period_us = 1000u * 1000u; // maximum is 1s
      const std::uint32_t quota_us = period_us *
                                     std::thread::hardware_concurrency() *
                                     std::clamp(quota, 0u, 100u) / 100u;
      std::ofstream{cpu_quota_path} << quota_us << std::endl;
      std::ofstream{cpu_period_path} << period_us << std::endl;

      std::string content_check;
      std::getline(std::ifstream{cpu_quota_path}, content_check);

      spdlog::debug(fmt::format(
          "Set {} cpu quota to {:d} (period: {:d}) successfully: '{}' ({})",
          name, quota, period_us, content_check,
          std::string_view{cpu_quota_path.c_str()}));
    } catch (const fs::filesystem_error &e) {
      spdlog::error(fmt::format("{}: cgroup v1 error: {} at {}", __func__,
                                e.what(), std::string_view{e.path1().c_str()}));
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      const auto &cpu_max_path = get_cgroup_path(name) / "cpu.max";
      if (!exists(cpu_max_path)) {
        errors = 1;
        spdlog::error(
            fmt::format("{}: cgroup v2 error: cpu.max not found at {}!",
                        __func__, std::string_view{cpu_max_path.c_str()}));
        break;
      }
      std::ofstream cpu_weight_file(cpu_max_path);
      std::uint32_t max_period = 100000u;
      std::uint32_t period = max_period * std::thread::hardware_concurrency() *
                             std::clamp(quota, 0u, 100u) / 100u;
      cpu_weight_file << fmt::format("{} {}", period, max_period) << std::endl;
      std::string content_check;
      std::getline(std::ifstream{cpu_max_path}, content_check);
      spdlog::debug(fmt::format(
          "Set {} cpu quota to {:d} (period: {:d}) successfully: '{}' ({})",
          name, quota, period, content_check,
          std::string_view{cpu_max_path.c_str()}));
    } catch (const fs::filesystem_error &e) {
      spdlog::error(fmt::format("{}: cgroup error: {} at {}", __func__,
                                e.what(), std::string_view{e.path1().c_str()}));
      errors = 1;
    }
    break;
  }

  return errors;
}

int32_t control_groups::add_pid_to_cgroup(pid_t              pid,
                                          const std::string &cgroup_name) {
  std::int32_t errors = 0;
  std::int32_t error_code = 0;
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::trace("{}: Skipping because no cgroup available", __func__);
    errors = 1;
    break;
  case cgroup_info::v1:
    try {
      const auto &cpu_procs_path = get_cgroup_path(cgroup_name) / "tasks";
      if (!exists(cpu_procs_path)) {
        errors = 1;
        spdlog::error(fmt::format("{}: cgroup path not found: {}", __func__,
                                  std::string_view{cpu_procs_path.c_str()}));
        break;
      }
      errno = 0;
      std::ofstream cpu_procs_file(cpu_procs_path);
      cpu_procs_file << std::max(pid, 0) << std::endl;
      error_code = errno;
      if (error_code == 0) {
        spdlog::debug(fmt::format("Added pid {:d} to {} successfully", pid,
                                  std::string_view{cpu_procs_path.c_str()}));
        break;
      }
      errors = 1;
      spdlog::error(fmt::format(
          "{}: cgroup v1 error: couldn't add task to cgroup {} ({})", __func__,
          std::string_view{cpu_procs_path.c_str()},
          std::string_view{strerror(error_code)}));
      break;
    } catch (const fs::filesystem_error &e) {
      spdlog::error(fmt::format("{}: cgroup v1 error: {} at {}", __func__,
                                e.what(), std::string_view{e.path1().c_str()}));
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      const auto &cpu_procs_path =
          get_cgroup_path(cgroup_name) / "cgroup.procs";
      if (!exists(cpu_procs_path)) {
        errors = 1;
        spdlog::error(fmt::format("{}: cgroup path not found: {}", __func__,
                                  std::string_view{cpu_procs_path.c_str()}));
        break;
      }
      errno = 0;
      std::fstream cpu_procs_file(cpu_procs_path);
      cpu_procs_file << std::max(pid, 0) << std::endl;
      error_code = errno;
      if (error_code == 0) {
        spdlog::debug(fmt::format("Added pid {:d} to {} successfully", pid,
                                  std::string_view{cpu_procs_path.c_str()}));
        break;
      }
      errors = 1;
      spdlog::error(
          fmt::format("{}: cgroup error: couldn't add task to cgroup {} ({})",
                      __func__, std::string_view{cpu_procs_path.c_str()},
                      std::string_view{strerror(error_code)}));
      break;
    } catch (const fs::filesystem_error &e) {
      spdlog::error(fmt::format("{}: cgroup error: {} at {}", __func__,
                                e.what(), std::string_view{e.path1().c_str()}));
      errors = 1;
    }
  }
  return errors;
}

control_groups::cgroup_info control_groups::get_cgroup_version(bool reset) {
  static std::mutex           info_mutex{};
  std::lock_guard<std::mutex> lock{info_mutex};

  static std::optional<cgroup_info> info{};

  if (reset) {
    info = std::nullopt;
  }

  if (!info.has_value()) {
    std::ifstream mtab("/proc/self/mounts");
    while (mtab) {
      std::string       device, fs_type, line;
      fs::path          cgroup_path;
      std::stringstream ss;
      std::getline(mtab, line);
      spdlog::trace("{}: line = {}", __func__, line);
      ss << line;
      ss >> device;
      ss >> cgroup_path;
      ss >> fs_type;

      try {
        if (fs_type == "cgroup2") {
          fs::path test_cgroup(cgroup_path);
          test_cgroup /= "ananicy_test_cgroup2";

          if (fs::exists(test_cgroup)) {
            if (!fs::remove(test_cgroup)) {
              spdlog::warn(
                  fmt::format("Couldn't delete {}",
                              std::string_view{test_cgroup.c_str()}));
            }
          }

          if (!create_directory(test_cgroup)) {
            spdlog::warn(
                fmt::format("can't create new cgroup(2) at {}, skipping",
                            std::string_view{cgroup_path.c_str()}));
            break;
          }

          const auto controllers_path =
              fs::path{test_cgroup} / "cgroup.controllers";
          std::ifstream controllers_file{controllers_path};
          // controllers_file.exceptions(std::fstream::failbit);

          bool has_cpu_controller = false;
          while (controllers_file) {
            std::string word;
            controllers_file >> word;
            if (word == "cpu") {
              spdlog::trace(
                  fmt::format("{}: Found {} controller in {}", __func__, word,
                              std::string_view{controllers_path.c_str()}));
              has_cpu_controller = true;
              break;
            }
          }

          if (!has_cpu_controller) {
            spdlog::warn(
                fmt::format("cgroup2 at {} doesn't have a cpu controller "
                            "available, skipping",
                            std::string_view{test_cgroup.c_str()}));
            break;
          }

          const auto &test_cgroup_cpumax = fs::path{test_cgroup} / "cpu.max";
          if (!fs::exists(test_cgroup_cpumax)) {
            spdlog::warn(fmt::format("cgroup2 at {} lacks cpu.max, skipping",
                                     std::string_view{test_cgroup.c_str()}));
            break;
          }
          // Cleanup the cgroup we created
          std::error_code err{};
          fs::remove(test_cgroup, err);
          info = {.path = cgroup_path,
                  .version = cgroup_info::cgroup_version::v2};
          spdlog::trace(fmt::format("Found cgroup v2 at {}",
                                    std::string_view{cgroup_path.c_str()}));
          break;
        } else if (fs_type == "cgroup") {
          if (!info.has_value()) {
            cgroup_path = cgroup_path.parent_path();
            if (!fs::exists(fs::path{cgroup_path} / "cpu")) {
              spdlog::warn(
                  fmt::format("cgroup at {} lack cpu controller, skipping",
                              std::string_view{cgroup_path.c_str()}));
              break;
            }
            info = {.path = cgroup_path,
                    .version = cgroup_info::cgroup_version::v1};
            spdlog::trace(fmt::format("Found cgroup v1 at {}",
                                      std::string_view{cgroup_path.c_str()}));
          }
        }
      } catch (const fs::filesystem_error &e) {
        spdlog::warn(fmt::format("{}: {} at {}", __func__, e.what(),
                                 std::string_view{e.path1().c_str()}));
        break;
      } catch (const std::ios::failure &e) {
        spdlog::warn(fmt::format("{}: {}, message: {}", __func__, e.what(),
                                 e.code().message()));
        break;
      }
      if (info.has_value()) {
        break;
      }
    }
    if (!info.has_value()) {
      spdlog::debug("No cgroup info, setting defaults");
      info = {.path = {""}, .version = cgroup_info::cgroup_version::none};
    }
    spdlog::debug(fmt::format("Cgroup info: {}, path: {}",
                              static_cast<int>(info->version),
                              std::string_view{info->path.c_str()}));
  }

  return info.value();
}

#if ENABLE_SYSTEMD == 1
extern "C" int sd_pid_get_cgroup(pid_t, char **cgroup);
#endif
std::string control_groups::get_cgroup_for_pid(pid_t pid) {
#if ENABLE_SYSTEMD == 1
  char *cgroup;
  sd_pid_get_cgroup(pid, &cgroup);
  std::string cgroup_name(cgroup);
  free(cgroup);

  return cgroup_name;
#else
  std::ifstream pid_cgroup_file(fmt::format("/proc/{:d}/cgroup", pid));
  std::string   line;
  std::getline(pid_cgroup_file, line);

  if (line.empty()) {
    return "<empty>";
  }

  auto result = line.substr(line.find_last_of("::") + 1);
  return result;
#endif
}
