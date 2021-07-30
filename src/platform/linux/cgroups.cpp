#include "core/cgroups.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

static std::filesystem::path get_cgroup_path(const std::string &cgroup_name) {
  auto  cgroup_info = control_groups::get_cgroup_version();
  auto &base_path = cgroup_info.path;

  spdlog::trace("{}: .path = {}, .version = {}", __func__,
                cgroup_info.path.string(), cgroup_info.version);

  std::filesystem::path cgroup_path{base_path};
  switch (cgroup_info.version) {
  case control_groups::cgroup_info::none:
    break;
  case control_groups::cgroup_info::v1:
    cgroup_path.append("cpu").append(cgroup_name);
    spdlog::trace("{}: cgroup_path: {}", __func__, cgroup_path.string());
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
      path cpu_max_path = get_cgroup_path(name).append("cpu.max");
      if (exists(cpu_max_path)) {
        errors = 1;
        spdlog::error("{}: cgroup v2 error: cpu.max not found!", __func__);
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
    std::fstream mtab("/etc/mtab");
    while (mtab) {
      std::string           word, line;
      std::filesystem::path cgroup_path;
      std::stringstream     ss;
      std::getline(mtab, line);
      spdlog::trace("{}: line = {}", __func__, line);
      ss << line;
      while (ss) {
        ss >> word;
        try {
          if (word.starts_with("cgroup2")) {
            ss >> cgroup_path;
            std::filesystem::path test_cgroup(cgroup_path);
            if (!create_directory(test_cgroup.append("ananicy_test_cgroup2"))) {
              spdlog::warn("cgroup2 at {} are non-functional, skipping",
                           cgroup_path.string());
              break;
            }
            if (!std::filesystem::exists(
                    std::filesystem::path(test_cgroup).append("cpu.max"))) {
              spdlog::warn("cgroup2 at {} lack cpu.max, skipping",
                           cgroup_path.string());
              break;
            }
            info = {.path = cgroup_path,
                    .version = cgroup_info::cgroup_version::v2};
            spdlog::trace("Found cgroup v2 at {}", cgroup_path.string());
            break;
          } else if (word.starts_with("cgroup")) {
            if (!info.has_value()) {
              ss >> cgroup_path;
              cgroup_path = cgroup_path.parent_path();
              if (!std::filesystem::exists(
                      std::filesystem::path(cgroup_path).append("cpu"))) {
                spdlog::warn("cgroup at {} lack cpu controller, skipping",
                             cgroup_path.string());
                break;
              }
              info = {.path = cgroup_path,
                      .version = cgroup_info::cgroup_version::v1};
              spdlog::trace("Found cgroup v1 at {}", cgroup_path.string());
            }
          }
        } catch (const std::filesystem::filesystem_error &e) {
          spdlog::warn("{}: {} at {}", __func__, e.what(), e.path1().string());
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
