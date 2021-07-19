#include "core/cgroups.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

inline std::filesystem::path get_cgroup_path(const std::string &cgroup_name) {
  return {std::format("/sys/fs/cgroup/{:s}", cgroup_name)};
}

int control_groups::create_cgroup(const std::string &name) {
  using namespace std::filesystem;
  path cgroup_path = get_cgroup_path(name);
  if (exists(cgroup_path)) {
    spdlog::warn("cgroup {:s} already exists, ignoring.", name);
    return false;
  }
  spdlog::debug("Creating cgroup {:s} at {:s}", name, cgroup_path.string());

  return create_directory(cgroup_path);
}

int control_groups::set_cgroup_cpu_quota(const std::string &name,
                                         unsigned           quota) {
  using namespace std::filesystem;
  int errors = 0;
  try {
    path          cpu_weight_path = get_cgroup_path(name).append("cpu.weight");
    std::ofstream cpu_weight_file(cpu_weight_path);
    cpu_weight_file << std::clamp(quota, 0u, 100u);
    spdlog::debug("Set {} cpu quota to {:d} successfully ({})", name, quota,
                  cpu_weight_path.string());
  } catch (const std::filesystem::filesystem_error &e) {
    spdlog::error("{}: cgroup error: {} at {}", __func__, e.what(),
                  e.path1().string());
    errors = 1;
  }
  return errors;
}

int control_groups::add_pid_to_cgroup(pid_t              pid,
                                      const std::string &cgroup_name) {
  using namespace std::filesystem;
  int errors = 0;
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
  return errors;
}
