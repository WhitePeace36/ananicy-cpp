#include <utility/process_info.hpp>

#include "core/priority.hpp"
#include "syscalls.h"

#include <charconv>
#include <concepts>
#include <core/process.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <spdlog/spdlog.h>
#include <string_view>

#include "process_helpers.hpp"

namespace fs = std::filesystem;

namespace process_info {

template <std::integral T = std::size_t>
static constexpr inline T to_int(const std::string_view &str) {
  T result = 0;
  std::from_chars(str.data(), str.data() + str.size(), result);
  return result;
}

template <std::integral T = std::size_t>
static constexpr inline std::string_view to_str(T number) {
  std::array<char, 35> str{};
  auto [ptr, _] = std::to_chars(str.data(), str.data() + str.size(), number);
  return std::string_view(std::to_address(str.data()), static_cast<std::size_t>(ptr - str.data()));
}

static autogroup get_autogroup_from_str(const std::string &str) {
  const auto begin_group_num = str.find_first_not_of("/autogroup-");
  if (begin_group_num == std::string::npos) {
    return nullptr;
  }
  const auto end_group_num = str.find_first_of(' ', begin_group_num);
  const auto begin_nice_num = str.find_first_not_of("nice ", end_group_num);
  const auto end_nice_num = str.size();

  return {{"group", to_int(str.substr(begin_group_num, end_group_num))},
          {"nice", to_int(str.substr(begin_nice_num, end_nice_num))}};
}

nlohmann::json get_autogroup_map() {
  nlohmann::json autogroup_map = {};

  const auto &process_info_map = get_process_info_map();

  for (auto process_info : process_info_map) {
    if (process_info["autogroup"] != nullptr) {
      const auto autogroup_num =
          to_str(static_cast<int>(process_info["autogroup"]["group"]));
      if (!autogroup_map.contains(autogroup_num)) {
        autogroup_map[autogroup_num.data()] = nlohmann::json::object();
        autogroup_map[autogroup_num.data()]["nice"] =
            process_info["autogroup"]["nice"];
        autogroup_map[autogroup_num.data()]["proc"] = nlohmann::json::object();
      }
      process_info.erase("autogroup");
      const auto pid = to_str(static_cast<int>(process_info["tpid"]));
      autogroup_map[autogroup_num.data()]["proc"][pid.data()] = process_info;
    }
  }

  return autogroup_map;
}

using file_time_point = std::chrono::file_clock::time_point;
static nlohmann::json parse_proc_status(process_id_t pid) {
  static std::unordered_map<process_id_t,
                            std::pair<file_time_point, nlohmann::json>>
      status_cache = {};

  fs::path status_file_path = std::format("/proc/{}/status", pid);
  file_time_point       status_file_last_modified{};
  try {
    status_file_last_modified = last_write_time(status_file_path);
    if (status_cache.contains(pid)) {
      if (status_file_last_modified <= status_cache[pid].first) {
        return status_cache[pid].second;
      }
    }
  } catch (const fs::filesystem_error &e) {
    return {};
  }

  std::ifstream  status_file{status_file_path};
  nlohmann::json status = nlohmann::json::object();

  std::string line;
  while (status_file.good()) {
    std::getline(status_file, line);
    const size_t      delimiter_pos = line.find_first_of(':');
    const std::string name = line.substr(0, delimiter_pos);
    std::string       value = line.substr(delimiter_pos + 1);

    const size_t first_non_whitespace = value.find_first_not_of({' ', '\t'});
    if (first_non_whitespace == std::string::npos) {
      value = "";
    } else {
      value = value.substr(first_non_whitespace); // Trim leading whitespace
    }
    status[name] = value;
  }

  status_cache[pid] = std::make_pair(status_file_last_modified, status);
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

static sched_attr get_sched_attributes(process_id_t pid) {

  ::sched_attr attr{};
  sched_getattr(static_cast<pid_t>(pid), &attr, sizeof(attr), 0);

  return attr;
}

bool is_realtime(process_id_t pid) {
  const sched_attr attr = get_sched_attributes(pid);
  return attr.sched_priority > 0;
}

bool is_kernel_process(process_id_t pid) {
  auto status = parse_proc_status(pid);
  return (status["PPid"] == "0") || (status["PPid"] == "2");
}

enum io_prio_class {
  NONE = IOPRIO_CLASS_NONE,
  REALTIME = IOPRIO_CLASS_RT,
  BEST_EFFORT = IOPRIO_CLASS_BE,
  IDLE = IOPRIO_CLASS_IDLE
};

struct io_prio_attributes {
  io_prio_class io_class;
  int           io_nice;
};

static io_prio_attributes get_io_prio_attributes(process_id_t pid) {
  const int io_prio_data = ioprio_get(IOPRIO_WHO_PROCESS, pid);
  const int io_nice = IOPRIO_PRIO_DATA(static_cast<std::size_t>(io_prio_data));

  io_prio_class io_class;
  switch (IOPRIO_PRIO_CLASS(io_prio_data)) {
  case IOPRIO_CLASS_IDLE:
    io_class = IDLE;
    break;
  case IOPRIO_CLASS_BE:
    io_class = BEST_EFFORT;
    break;
  case IOPRIO_CLASS_RT:
    io_class = REALTIME;
    break;
  default:
    io_class = NONE;
    break;
  }

  return io_prio_attributes{.io_class = io_class, .io_nice = io_nice};
}

static std::optional<process_info> get_process_info(process_id_t pid) {
  if (!exists(fs::path(std::format("/proc/{}", pid))))
    return {};

  process_info info = nlohmann::json::object();

  try {
    std::error_code ec;

    nlohmann::json status = parse_proc_status(pid);
    if (status["PPid"] == 0) {
      spdlog::trace("{}: {} is a kernel thread, skipping", __func__, pid);
      return std::optional<process_info>();
    }

    info["pid"] = to_int(static_cast<std::string>(status["Tgid"]));

    info["tpid"] = pid;

    info["exe"] = fs::read_symlink(std::format("/proc/{}/exe", pid), ec);
    if (ec)
      info["exe"] = nullptr;

    std::string comm;
    std::getline(std::ifstream(std::format("/proc/{}/comm", pid)), comm);
    info["comm"] = comm;

    info["cmd"] = get_command_from_pid(static_cast<pid_t>(pid));

    std::string stat;
    std::getline(std::ifstream(std::format("/proc/{}/stat", pid)), stat);
    info["stat"] = stat;

    const auto stat_name_begin = stat.find_first_of('(') + 1;
    const auto stat_name_end = stat.find_first_of(')');

    info["stat_name"] =
        stat.substr(stat_name_begin, stat_name_end - stat_name_begin);

    std::string autogroup_value;
    std::getline(std::ifstream(std::format("/proc/{}/autogroup", pid)),
                 autogroup_value);
    info["autogroup"] = get_autogroup_from_str(autogroup_value);

    auto sched_info = get_sched_attributes(pid);

    info["sched"] = get_sched_policy_name(sched_info.sched_policy);
    info["rtprio"] = uint32_t(sched_info.sched_priority);
    info["nice"] = int(sched_info.sched_nice);
    info["latency_nice"] = int(sched_info.sched_latency_nice);

    const auto io_data = get_io_prio_attributes(pid);

    info["ionice"] = nlohmann::json::array();

    std::string io_class_name{};
    switch (io_data.io_class) {
    case NONE:
      io_class_name = "none";
      break;
    case REALTIME:
      io_class_name = "realtime";
      break;
    case BEST_EFFORT:
      io_class_name = "best-effort";
      break;
    case IDLE:
      io_class_name = "idle";
      break;
    }

    info["ionice"][0] = io_class_name;
    if (io_data.io_class == BEST_EFFORT) {
      info["ionice"][1] = io_data.io_nice;
    } else {
      info["ionice"][1] = nullptr;
    }

    unsigned oom_score_adj = 0;
    std::ifstream{std::format("/proc/{}/oom_score_adj", pid)} >> oom_score_adj;
    info["oom_score_adj"] = oom_score_adj;

    info["cmdline"] = get_cmdline_from_pid(static_cast<int>(pid));

  } catch (const fs::filesystem_error &e) {
    spdlog::trace("{}: Error getting {} infos: {} ({}, {})", __func__, pid,
                  e.what(), e.path1().string(), e.path2().string());

    info["incomplete"] = true;
  } catch (const std::exception &e) {
    spdlog::trace("{:s}: Error getting {:d} infos: {}", __func__, pid,
                  e.what());
    info["incomplete"] = true;
  }

  return {info};
}

nlohmann::json get_process_info_map() {
  nlohmann::json process_map = nlohmann::json::object();

  std::queue<fs::path> directories{};
  for (const fs::path &process_path : fs::directory_iterator("/proc")) {

    if (!isdigit(process_path.filename().string()[0])) {
      spdlog::trace("{}: Skipping entry '{}'", __func__, process_path.string());
      continue;
    }

    const auto pid = to_int(process_path.filename().string());
    if (pid == 0 || process_map.contains(to_str(pid)))
      continue;

    directories.emplace(fs::path(std::format("/proc/{}/task", pid)));
  }

  while (!directories.empty()) {
    const auto &directory = directories.front();

    try {
      for (const fs::path &process_path : fs::directory_iterator(directory)) {
        if (!isdigit(process_path.filename().string()[0])) {
          spdlog::trace("{}: Skipping entry '{}'", __func__,
                        process_path.string());
          continue;
        }

        const auto pid = to_int(process_path.filename().string());
        if (pid == 0 || process_map.contains(to_str(pid)))
          continue;

        std::optional<process_info> info =
            get_process_info(static_cast<uint32_t>(pid));
        if (info.has_value()) {
          process_map[to_str(pid).data()] = info.value();
        }
      }
    } catch (...) {
    }

    directories.pop();
  }

  return process_map;
}
} // namespace process_info
