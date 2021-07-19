#pragma once

#include <string>

namespace control_groups {
int create_cgroup(const std::string &name);
int set_cgroup_cpu_quota(const std::string &name, unsigned quota);
int add_pid_to_cgroup(pid_t pid, const std::string &cgroup_name);
} // namespace control_groups