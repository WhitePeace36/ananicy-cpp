#include <utility/process_info.hpp>

#include "core/priority.hpp"
#include "syscalls.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <spdlog/spdlog.h>

namespace process_info {

static autogroup get_autogroup_from_str(const std::string &str) {
  auto begin_group_num = str.find_first_not_of("/autogroup-");
  if (begin_group_num == std::string::npos) {
    return nullptr;
  }
  auto end_group_num = str.find_first_of(' ', begin_group_num);
  auto begin_nice_num = str.find_first_not_of("nice ", end_group_num);
  auto end_nice_num = str.size();

  return {{"group", std::stol(str.substr(begin_group_num, end_group_num))},
          {"nice", std::stol(str.substr(begin_nice_num, end_nice_num))}};
}

std::map<unsigned, autogroup> get_autogroup_map() {
  std::map<unsigned, autogroup> autogroup_map = {};

  return autogroup_map;
}

nlohmann::json parse_proc_status(unsigned pid) {
  nlohmann::json status = nlohmann::json::object();

  std::fstream status_file = std::fstream(std::format("/proc/{}/status", pid));
  std::string  line;
  while (status_file.good()) {
    std::getline(status_file, line);
    size_t      delimiter_pos = line.find_first_of(':');
    std::string name = line.substr(0, delimiter_pos);
    std::string value = line.substr(delimiter_pos + 1);

    size_t first_non_whitespace = value.find_first_not_of({' ', '\t'});
    if (first_non_whitespace == std::string::npos) {
      value = "";
    } else {
      value = value.substr(first_non_whitespace); // Trim leading whitespace
    }
    status[name] = value;
  }
  std::cerr << __func__ << ":\n" << status.dump(4) << std::endl;
  return status;
}

static std::string get_sched_policy_name(unsigned sched_policy) {
  switch (sched_policy & 0xFF) {
  case SCHED_RR:
    return "rr";
  case SCHED_DEADLINE:
    return "deadline";
  case SCHED_FIFO:
    return "fifo";
  case SCHED_OTHER:
    return "normal";
  case SCHED_BATCH:
    return "batch";
  case SCHED_IDLE:
    return "idle";
  case SCHED_ISO:
    return "iso";
  default:
    return "unknown";
  }
}

static sched_attr get_sched_attributes(unsigned int pid) {

  ::sched_attr attr{};
  sched_getattr(static_cast<pid_t>(pid), &attr, sizeof(attr), 0);

  return attr;
}

static std::optional<process_info> get_process_info(unsigned pid) {
  if (!exists(std::filesystem::path(std::format("/proc/{}", pid))))
    return std::optional<process_info>();

  process_info info = nlohmann::json::object();

  try {
    nlohmann::json status = parse_proc_status(pid);
    if (status["PPid"] == 0) {
      spdlog::trace("{}: {} is a kernel thread, skipping", __func__, pid);
      return std::optional<process_info>();
    }

    info["pid"] = std::stoul(static_cast<std::string>(status["Tgid"]));

    info["tpid"] = pid;

    try {
      info["exe"] =
          std::filesystem::read_symlink(std::format("/proc/{}/exe", pid));
    } catch (const std::filesystem::filesystem_error &e) {
      info["exe"] = nullptr;
    }

    info["cmd"] = nullptr;

    std::string stat;
    std::getline(std::fstream(std::format("/proc/{}/stat", pid)), stat);
    info["stat"] = stat;

    auto stat_name_begin = stat.find_first_of('(');
    auto stat_name_end = stat.find_first_of(')');

    info["stat_name"] =
        stat.substr(stat_name_begin, stat_name_end - stat_name_begin);

    std::string autogroup_value;
    std::getline(std::fstream(std::format("/proc/{}/autogroup", pid)),
                 autogroup_value);
    info["autogroup"] = get_autogroup_from_str(autogroup_value);

    ::sched_attr sched_info = get_sched_attributes(pid);

    info["sched"] = get_sched_policy_name(sched_info.sched_policy);
    info["rtprio"] = static_cast<unsigned>(sched_info.sched_priority);
    info["nice"] = static_cast<int>(sched_info.sched_nice);

    info["ionice"] = ioprio_get(IOPRIO_WHO_PROCESS, pid);

    unsigned oom_score_adj = 0;
    std::fstream(std::fstream(std::format("/proc/{}/oom_score_adj", pid))) >>
        oom_score_adj;
    info["oom_score_adj"] = oom_score_adj;

    std::string cmdline_value;
    std::getline(std::fstream(std::format("/proc/{}/cmdline", pid)),
                 cmdline_value);
    std::vector<std::string> cmdline;

    size_t start_pos;
    size_t end_pos = 0;
    while ((start_pos = cmdline_value.find_first_not_of('\0', end_pos)) !=
           std::string::npos) {
      end_pos = cmdline_value.find('\0', start_pos);
      cmdline.push_back(cmdline_value.substr(start_pos, end_pos - start_pos));
    }

    info["cmdline"] = cmdline;

    //    if (status["Tgid"] != status["Pid"]) {
    //      std::cout << "Found different process:\n" << info.dump(4) <<
    //      std::endl; std::exit(10);
    //    }

  } catch (const std::filesystem::filesystem_error &e) {
    spdlog::trace("{}: Error getting {} infos: {} ({}, {})", __func__, pid,
                  e.what(), e.path1().string(), e.path2().string());

    info["incomplete"] = true;

    return std::optional<process_info>(info);
  }

  return std::optional<process_info>(info);
}

nlohmann::json get_process_info_map() {
  nlohmann::json process_map = nlohmann::json::object();

  std::queue<std::filesystem::path> directories{};
  for (const std::filesystem::path &process_path :
       std::filesystem::directory_iterator("/proc")) {

    if (!isdigit(process_path.filename().string()[0])) {
      spdlog::trace("{}: Skipping entry '{}'", __func__, process_path.string());
      continue;
    }

    unsigned pid = std::stoul(process_path.filename().string());

    if (pid == 0 || process_map.contains(std::to_string(pid)))
      continue;

    directories.emplace(
        std::filesystem::path(std::format("/proc/{}/task", pid)));
  }

  while (!directories.empty()) {
    const auto &directory = directories.front();

    try {
      for (const std::filesystem::path &process_path :
           std::filesystem::directory_iterator(directory)) {
        if (!isdigit(process_path.filename().string()[0])) {
          spdlog::trace("{}: Skipping entry '{}'", __func__,
                        process_path.string());
          continue;
        }
        unsigned pid = std::stoul(process_path.filename().string());

        if (pid == 0 || process_map.contains(std::to_string(pid)))
          continue;

        std::optional<process_info> info = get_process_info(pid);
        if (info.has_value()) {
          process_map[std::to_string(pid)] = info.value();
        }
      }
    } catch (const std::filesystem::filesystem_error &e) {
    }

    directories.pop();
  }

  return process_map;
}
} // namespace process_info