#include "core/cgroups.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

namespace fs = std::filesystem;

static fs::path get_cgroup_path(const std::string_view &cgroup_name) {
  const auto &cgroup_info = control_groups::get_cgroup_version();
  const auto &base_path = cgroup_info.path;

  spdlog::trace("{}: .path = {}, .version = {}", __func__,
                cgroup_info.path.string(), cgroup_info.version);

  fs::path cgroup_path{base_path};
  switch (cgroup_info.version) {
  case control_groups::cgroup_info::none:
    break;
  case control_groups::cgroup_info::v1:
    cgroup_path /= fs::path{"cpu"} / cgroup_name.data();
    spdlog::trace("{}: cgroup_path: {}", __func__, cgroup_path.string());
    break;
  case control_groups::cgroup_info::v2:
    cgroup_path /= cgroup_name;
    break;
  }
  return cgroup_path;
}

int control_groups::create_cgroup(const std::string &name) {
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::warn(
        "Cgroups are not available on this platform (or are not enabled)");
    break;
  case cgroup_info::v1: {
    const auto &cgroup_path = get_cgroup_path(name);
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup v1 {:s} already exists, ignoring.", name);
      return false;
    }
    spdlog::debug("Creating cgroup (v1) {:s} at {:s}", name,
                  cgroup_path.string());

    return create_directory(cgroup_path);
  }
  case cgroup_info::v2: {
    const auto &cgroup_path = get_cgroup_path(name);
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup {:s} already exists, ignoring.", name);
      return false;
    }
    spdlog::debug("Creating cgroup {:s} at {:s}", name, cgroup_path.string());

    return create_directory(cgroup_path);
  }
  }
  return false;
}

int control_groups::set_cgroup_cpu_quota(const std::string &name,
                                         unsigned           quota) {
  int errors = 0;
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::trace("{}: Skipping because no cgroup available", __func__);
    errors = 1;
    break;
  case cgroup_info::v1:
    try {
      const auto &cpu_quota_path =
          get_cgroup_path(name) / "cpu.cfs_quota_us";
      const auto &cpu_period_path =
          get_cgroup_path(name) / "cpu.cfs_period_us";
      unsigned period_us = 1000u * 1000u; // maximum is 1s
      unsigned quota_us = period_us * std::thread::hardware_concurrency() *
                          std::clamp(quota, 0u, 100u) / 100u;
      std::ofstream{cpu_quota_path} << quota_us << std::endl;
      std::ofstream{cpu_period_path} << period_us << std::endl;

      std::string content_check;
      std::getline(std::ifstream{cpu_quota_path}, content_check);

      spdlog::debug(
          "Set {} cpu quota to {:d} (period: {:d}) successfully: '{}' ({})",
          name, quota, period_us, content_check, cpu_quota_path.string());
    } catch (const fs::filesystem_error &e) {
      spdlog::error("{}: cgroup v1 error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      const auto &cpu_max_path = get_cgroup_path(name) / "cpu.max";
      if (!exists(cpu_max_path)) {
        errors = 1;
        spdlog::error("{}: cgroup v2 error: cpu.max not found at {}!", __func__,
                      cpu_max_path.string());
        break;
      }
      std::ofstream cpu_weight_file(cpu_max_path);
      unsigned      max_period = 100000u;
      unsigned      period = max_period * std::thread::hardware_concurrency() *
                        std::clamp(quota, 0u, 100u) / 100u;
      cpu_weight_file << std::format("{} {}", period, max_period) << std::endl;
      std::string content_check;
      std::getline(std::ifstream{cpu_max_path}, content_check);
      spdlog::debug(
          "Set {} cpu quota to {:d} (period: {:d}) successfully: '{}' ({})",
          name, quota, period, content_check, cpu_max_path.string());
    } catch (const fs::filesystem_error &e) {
      spdlog::error("{}: cgroup error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
    break;
  }

  return errors;
}

int control_groups::add_pid_to_cgroup(pid_t              pid,
                                      const std::string &cgroup_name) {
  int errors = 0;
  int error_code = 0;
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
        spdlog::error("{}: cgroup path not found: {}", __func__,
                      cpu_procs_path.string());
        break;
      }
      errno = 0;
      std::ofstream cpu_procs_file(cpu_procs_path);
      cpu_procs_file << std::max(pid, 0) << std::endl;
      error_code = errno;
      if (cpu_procs_file) {
        spdlog::debug("Added pid {:d} to {} successfully", pid,
                      cpu_procs_path.string());
        break;
      }
      errors = 1;
      spdlog::error("{}: cgroup v1 error: couldn't add task to cgroup {} ({})",
                    __func__, cpu_procs_path.string(), strerror(error_code));
      break;
    } catch (const fs::filesystem_error &e) {
      spdlog::error("{}: cgroup v1 error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      const auto &cpu_procs_path =
          get_cgroup_path(cgroup_name) / "cgroup.procs";
      if (!exists(cpu_procs_path)) {
        errors = 1;
        spdlog::error("{}: cgroup path not found: {}", __func__,
                      cpu_procs_path.string());
        break;
      }
      errno = 0;
      std::fstream cpu_procs_file(cpu_procs_path);
      cpu_procs_file << std::max(pid, 0) << std::endl;
      error_code = errno;
      if (cpu_procs_file) {
        spdlog::debug("Added pid {:d} to {} successfully", pid,
                      cpu_procs_path.string());
        break;
      }
      errors = 1;
      spdlog::error("{}: cgroup error: couldn't add task to cgroup {} ({})",
                    __func__, cpu_procs_path.string(), strerror(error_code));
      break;
    } catch (const fs::filesystem_error &e) {
      spdlog::error("{}: cgroup error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
  }
  return errors;
}

control_groups::cgroup_info control_groups::get_cgroup_version(bool reset) {
  using namespace std::chrono_literals;

  static std::optional<cgroup_info> info{};

  if (reset) {
    info.reset();
  }

  if (!info.has_value()) {
    std::ifstream mtab("/etc/mtab");
    while (mtab) {
      std::string       word, line;
      fs::path          cgroup_path;
      std::stringstream ss;
      std::getline(mtab, line);
      spdlog::trace("{}: line = {}", __func__, line);
      ss << line;
      while (ss) {
        ss >> word;
        try {
          if (word.starts_with("cgroup2")) {
            ss >> cgroup_path;
            fs::path test_cgroup(cgroup_path);
            test_cgroup /= "ananicy_test_cgroup2";

            if (fs::exists(test_cgroup)) {
              if (!fs::remove(test_cgroup)) {
                spdlog::warn("Couldn't delete {}", test_cgroup.string());
              }
            }

            if (!create_directory(test_cgroup)) {
              spdlog::warn("can't create new cgroup(2) at {}, skipping",
                           cgroup_path.string());
              break;
            }

            const auto controllers_path = fs::path{test_cgroup} / "cgroup.controllers";
            std::ifstream controllers_file{controllers_path};
            // controllers_file.exceptions(std::fstream::failbit);

            bool has_cpu_controller = false;
            while (controllers_file) {
              controllers_file >> word;
              if (word == "cpu") {
                spdlog::trace("{}: Found {} controller in {}", __func__, word,
                              controllers_path.string());
                has_cpu_controller = true;
                break;
              }
            }

            if (!has_cpu_controller) {
              spdlog::warn("cgroup2 at {} doesn't have a cpu controller "
                           "available, skipping",
                           test_cgroup.string());
              break;
            }

            const auto &test_cgroup_cpumax = fs::path{test_cgroup} / "cpu.max";
            if (!fs::exists(test_cgroup_cpumax)) {
              spdlog::warn("cgroup2 at {} lacks cpu.max, skipping",
                           test_cgroup.string());
              break;
            }
            // Cleanup the cgroup we created
            std::error_code err{};
            fs::remove(test_cgroup, err);
            info = {.path = cgroup_path,
                    .version = cgroup_info::cgroup_version::v2};
            spdlog::trace("Found cgroup v2 at {}", cgroup_path.string());
            break;
          } else if (word.starts_with("cgroup")) {
            if (!info.has_value()) {
              ss >> cgroup_path;
              cgroup_path = cgroup_path.parent_path();
              if (!fs::exists(fs::path{cgroup_path} / "cpu")) {
                spdlog::warn("cgroup at {} lack cpu controller, skipping",
                             cgroup_path.string());
                break;
              }
              info = {.path = cgroup_path,
                      .version = cgroup_info::cgroup_version::v1};
              spdlog::trace("Found cgroup v1 at {}", cgroup_path.string());
            }
          }
        } catch (const fs::filesystem_error &e) {
          spdlog::warn("{}: {} at {}", __func__, e.what(), e.path1().string());
          break;
        } catch (const std::ios::failure &e) {
          spdlog::warn("{}: {}, message: {}", __func__, e.what(),
                       e.code().message());
          break;
        }
      }
      if (info.has_value()) {
        break;
      }
    }
    if (!info.has_value()) {
      spdlog::debug("No cgroup info, setting defaults");
      info = {.path = {""}, .version = cgroup_info::cgroup_version::none};
    }
    spdlog::debug("Cgroup info: {}, path: {}", info->version,
                  info->path.string());
  }

  return info.value();
}

#if ENABLE_SYSTEMD == 1
extern "C" int sd_pid_get_cgroup(pid_t, char **cgroup);
#endif
std::string    control_groups::get_cgroup_for_pid(pid_t pid) {
#if ENABLE_SYSTEMD == 1
  char *cgroup;
  sd_pid_get_cgroup(pid, &cgroup);
  std::string cgroup_name(cgroup);
  free(cgroup);

  return cgroup_name;
#else
  std::ifstream pid_cgroup_file(std::format("/proc/{:d}/cgroup", pid));
  std::string   line;
  std::getline(pid_cgroup_file, line);

  if (line.empty()) {
    return "<empty>";
  }

  auto result = line.substr(line.find_last_of("::") + 1);
  return result;
#endif
}
