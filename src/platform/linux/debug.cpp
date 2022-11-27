#include "utility/debug.hpp"
#include "utility/utils.hpp"

#include "core/cgroups.hpp"
#include "service.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
#include <unistd.h>

namespace ananicy_debug {
void print_file(const std::string &path) {
  const auto file_data = read_file(path);

  std::cout << std::format("#### BEGIN {0} #####\n"
                           "{1:s}\n"
                           "#### END {0} #####\n",
                           path, file_data);
}

template <> void print_debug_for_issue<21>() {
  print_file("/etc/mtab");

  namespace fs = std::filesystem;

  try {
    const auto &cgroup_info = control_groups::get_cgroup_version();
    const auto &cgroup_path = cgroup_info.path.c_str();

    if (cgroup_info.version != control_groups::cgroup_info::none) {
      std::cout << std::format("#### BEGIN listing files in {} #####\n",
                               cgroup_path);
      for (const auto &dir_entry : fs::directory_iterator(cgroup_path)) {
        std::cout << dir_entry << '\n';
      }
      std::cout << std::format("#### END listing files in {} #####\n",
                               cgroup_path);
    }
  } catch (const std::exception &e) {
    spdlog::warn("{}: error: {}", __func__, e.what());
  }

  std::cout << std::format("Unit name: {}\n", service::get_unit_name());
  std::cout << std::format("Cgroup: {}\n",
                           control_groups::get_cgroup_for_pid(getpid()));
}
} // namespace ananicy_debug
