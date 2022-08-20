#pragma once
#include <string>
#include <vector>

namespace process_info {
  static std::vector<std::string> get_cmdline_from_pid(int pid) {
    std::string cmdline_value;
    std::getline(std::ifstream(std::format("/proc/{}/cmdline", pid)),
                 cmdline_value);
    std::vector<std::string> cmdline;

    size_t start_pos;
    size_t end_pos = 0;
    while ((start_pos = cmdline_value.find_first_not_of('\0', end_pos)) !=
           std::string::npos) {
      end_pos = cmdline_value.find('\0', start_pos);
      cmdline.push_back(cmdline_value.substr(start_pos, end_pos - start_pos));
    }

    return cmdline;
  }
}
