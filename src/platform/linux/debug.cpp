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

  try {
    const auto cgroup_info = control_groups::get_cgroup_version();
    if (cgroup_info.version != control_groups::cgroup_info::none) {
      std::cout << std::format("#### BEGIN listing files in {0} #####\n",
                               cgroup_info.path.string());
      for (const auto &file :
           std::filesystem::directory_iterator(cgroup_info.path)) {
        std::cout << file << std::endl;
      }
      std::cout << std::format("#### END listing files in {0} #####\n",
                               cgroup_info.path.string());
    }
  } catch (const std::exception &e) {
    spdlog::warn("{}: error: {}", __func__, e.what());
  }

  std::cout << std::format("Unit name: {}", service::get_unit_name())
            << std::endl;
  std::cout << std::format("Cgroup: {}",
                           control_groups::get_cgroup_for_pid(getpid()))
            << std::endl;
}
} // namespace ananicy_debug