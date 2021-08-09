#include "utility/debug.hpp"
#include "utility/utils.hpp"

#include "core/cgroups.hpp"
#include "service.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

namespace ananicy_debug {
void print_file(const std::string &path) {
  auto file_data = read_file(path);

  std::cout << std::format("#### BEGIN {0} #####\n"
                           "{1:s}\n"
                           "#### END {0} #####",
                           path, file_data)
            << std::endl;
}

template <> void print_debug_for_issue<21>() {
  print_file("/etc/mtab");

  const auto cgroup_path = control_groups::get_cgroup_version().path;
  std::cout << std::format("#### BEGIN listing files in {0} #####\n",
                           cgroup_path.string());
  for (const auto &file : std::filesystem::directory_iterator(cgroup_path)) {
    std::cout << file << std::endl;
  }
  std::cout << std::format("#### END listing files in {0} #####\n",
                           cgroup_path.string());

  std::cout << std::format("Unit name: {}", service::get_unit_name())
            << std::endl;
  std::cout << std::format("Cgroup: {}",
                           control_groups::get_cgroup_for_pid(getpid()))
            << std::endl;
}
} // namespace ananicy_debug