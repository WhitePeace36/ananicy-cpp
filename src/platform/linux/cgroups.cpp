#include "core/cgroups.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

inline std::filesystem::path get_cgroup_path(const std::string &cgroup_name) {
  auto  cgroup_info = control_groups::get_cgroup_version();
  auto &base_path = cgroup_info.path;

  std::filesystem::path cgroup_path;
  switch (cgroup_info.version) {
  case control_groups::cgroup_info::none:
    break;
  case control_groups::cgroup_info::v1:
    cgroup_path.append("cpu").append(cgroup_name);
    break;
  case control_groups::cgroup_info::v2:
    cgroup_path.append(cgroup_name);
    break;
  }
  return cgroup_path;
}

int control_groups::create_cgroup(const std::string &name) {
  using namespace std::filesystem;

  path cgroup_path;

  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    cgroup_path = get_cgroup_path(name);
    spdlog::warn(
        "Cgroups are not available on this platform (or are not enabled)");
    break;
  case cgroup_info::v1:
    cgroup_path = get_cgroup_path(name);
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup v1 {:s} already exists, ignoring.", name);
      return false;
    }
    spdlog::debug("Creating cgroup (v1) {:s} at {:s}", name,
                  cgroup_path.string());

    return create_directory(cgroup_path);
  case cgroup_info::v2:
    if (exists(cgroup_path)) {
      spdlog::warn("cgroup {:s} already exists, ignoring.", name);
      return false;
    }
    spdlog::debug("Creating cgroup {:s} at {:s}", name, cgroup_path.string());

    return create_directory(cgroup_path);
  }
  return false;
}

int control_groups::set_cgroup_cpu_quota(const std::string &name,
                                         unsigned           quota) {
  using namespace std::filesystem;
  int errors = 0;
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::trace("{}: Skipping because no cgroup available", __func__);
    errors = 1;
    break;
  case cgroup_info::v1:
    try {
      path cpu_quota_path = get_cgroup_path(name).append("cpu.cfs_quota_us");
      path cpu_period_path = get_cgroup_path(name).append("cpu.cfs_period_us");
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
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error("{}: cgroup v1 error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      path          cpu_weight_path = get_cgroup_path(name).append("cpu.max");
      std::ofstream cpu_weight_file(cpu_weight_path);
      unsigned      max_period = 100000u;
      unsigned      period = max_period * std::thread::hardware_concurrency() *
                        std::clamp(quota, 0u, 100u) / 100u;
      cpu_weight_file << std::format("{} {}", period, max_period) << std::endl;
      std::string content_check;
      std::getline(std::ifstream{cpu_weight_path}, content_check);
      spdlog::debug(
          "Set {} cpu quota to {:d} (period: {:d}) successfully: '{}' ({})",
          name, quota, period, content_check, cpu_weight_path.string());
    } catch (const std::filesystem::filesystem_error &e) {
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
  using namespace std::filesystem;
  int errors = 0;
  switch (get_cgroup_version().version) {

  case cgroup_info::none:
    spdlog::trace("{}: Skipping because no cgroup available", __func__);
    errors = 1;
    break;
  case cgroup_info::v1:
    try {
      path cpu_procs_path = get_cgroup_path(cgroup_name).append("tasks");
      std::ofstream(cpu_procs_path) << std::max(pid, 0) << std::endl;
      spdlog::debug("Added pid {:d} to {} successfully", pid,
                    cpu_procs_path.string());
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error("{}: cgroup v1 error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
    break;
  case cgroup_info::v2:
    try {
      path cpu_procs_path = get_cgroup_path(cgroup_name).append("cgroup.procs");
      std::fstream cpu_procs_file(cpu_procs_path);
      cpu_procs_file << std::max(pid, 0);
      spdlog::debug("Added pid {:d} to {} successfully", pid,
                    cpu_procs_path.string());
    } catch (const std::filesystem::filesystem_error &e) {
      spdlog::error("{}: cgroup error: {} at {}", __func__, e.what(),
                    e.path1().string());
      errors = 1;
    }
  }
  return errors;
}

control_groups::cgroup_info control_groups::get_cgroup_version() {
  static std::optional<cgroup_info> info{};
  if (!info.has_value()) {
    std::string  cgroup_path;
    std::fstream mtab("/etc/mtab");
    while (mtab) {
      std::string word;
      mtab >> word;
      if (word.starts_with("cgroup2")) {
        mtab >> cgroup_path;
        info = {.path = cgroup_path,
                .version = cgroup_info::cgroup_version::v2};
        spdlog::trace("Found cgroup v2 at {}", cgroup_path);
        break;
      } else if (word.starts_with("cgroup")) {
        if (!info.has_value()) {
          mtab >> cgroup_path;
          info = {.path = cgroup_path,
                  .version = cgroup_info::cgroup_version::v1};
          spdlog::trace("Found cgroup v1 at {}", cgroup_path);
        }
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
