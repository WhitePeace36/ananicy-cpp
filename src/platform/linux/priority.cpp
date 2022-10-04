//
// Created by aviallon on 20/04/2021.
//

#include "core/priority.hpp"
#include "syscalls.h"
#include <cerrno>
#include <charconv>
#include <concepts>
#include <filesystem>
#include <format>
#include <fstream>
#include <sched.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <type_traits>

namespace fs = std::filesystem;

/*
 * Latency nice is meant to provide scheduler hints about the relative
 * latency requirements of a task with respect to other tasks.
 * Thus a task with latency_nice == 19 can be hinted as the task with no
 * latency requirements, in contrast to the task with latency_nice == -20
 * which should be given priority in terms of lower latency.
 */
#define MAX_LATENCY_NICE   19
#define MIN_LATENCY_NICE   -20

#define LATENCY_NICE_WIDTH \
   (MAX_LATENCY_NICE - MIN_LATENCY_NICE + 1)

/*
 * Default tasks should be treated as a task with latency_nice = 0.
 */
#define DEFAULT_LATENCY_NICE   0

#define DEFAULT_LATENCY_NICE   0
#define DEFAULT_LATENCY_PRIO   (DEFAULT_LATENCY_NICE + LATENCY_NICE_WIDTH/2)

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static latency [ 0..39 ],
 * and back.
 */
#define NICE_TO_LATENCY(nice)  ((nice) + DEFAULT_LATENCY_PRIO)
#define LATENCY_TO_NICE(prio)  ((prio) - DEFAULT_LATENCY_PRIO)

template <std::integral T = std::size_t>
static constexpr inline T to_int(const std::string_view &str) {
  T result = 0;
  std::from_chars(str.data(), str.data() + str.size(), result);
  return result;
}

static int test_errno(int errcode, const std::string_view &func_name,
                      pid_t pid) {
  if (errcode == 0) {
    spdlog::debug("{}: Successfully applied to {}", func_name, pid);
    return 1;
  }

  if (errcode == ESRCH) {
    spdlog::warn("{}: Process not found: {}", func_name, pid);
    return 0;
  } else if (errcode == EACCES || errcode == EPERM) {
    throw std::runtime_error(std::format(
        "{}: Insufficient permission to set io-priority of process {}",
        func_name, pid));
  }
  spdlog::error("{}: Unknown error, errno is {}({})", func_name,
                strerror(errcode), errcode);
  return -1;
}

namespace priority {

bool set_priority(pid_t pid, int nice_value) {
  int errcode = 0;
  try {
    for (const auto &path :
         fs::directory_iterator(std::format("/proc/{}/task", pid))) {
      const int tid = static_cast<int>(to_int(path.path().filename().string()));
      errno = 0;
      if (tid > 0)
        setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice_value);
      errcode = errno;
    }
  } catch (const fs::filesystem_error &e) {
    spdlog::debug("{}: filesystem_error: {} (path: {})", __func__, e.what(),
                  e.path1().string());
    return 0;
  } catch (const std::exception &e) {
    spdlog::critical("{}: unknown exception: {}", __func__, e.what());
  }

  return test_errno(errcode, __func__, pid);
}

bool set_latency_nice(pid_t pid, int latency_nice_value) {
  int errcode = 0;
  try {
    for (const auto &path :
         fs::directory_iterator(std::format("/proc/{}/task", pid))) {
      const int tid = static_cast<int>(to_int(path.path().filename().string()));
      errno = 0;
      if (tid > 0)
        set_latnice(tid, latency_nice_value);
      errcode = errno;
    }
  } catch (const fs::filesystem_error &e) {
    spdlog::debug("{}: filesystem_error: {} (path: {})", __func__, e.what(),
                  e.path1().string());
    return 0;
  } catch (const std::exception &e) {
    spdlog::critical("{}: unknown exception: {}", __func__, e.what());
  }

  return test_errno(errcode, __func__, pid);
}

int get_latency_nice(pid_t pid) {
    return get_latnice(pid);
}

int convert_nice_to_lat(int nice_value) {
    return NICE_TO_LATENCY(nice_value);
}

int convert_lat_to_nice(int lat_value) {
    return LATENCY_TO_NICE(lat_value);
}

bool set_io_priority(pid_t pid, const std::string_view &io_class, int value) {
  int io_class_value = 0;
  if (io_class == "best-effort") {
    io_class_value = IOPRIO_CLASS_BE;
  } else if (io_class == "realtime") {
    io_class_value = IOPRIO_CLASS_RT;
  } else if (io_class == "idle") {
    io_class_value = IOPRIO_CLASS_IDLE;
  } else if (io_class == "none") {
    io_class_value = IOPRIO_CLASS_NONE;
  } else {
    spdlog::error("Unknown io class {}, skipping...", io_class);
    return true;
  }

  const int io_prio = IOPRIO_PRIO_VALUE(io_class_value, value);

  if (!ioprio_valid(io_prio)) {
    spdlog::error("IO priority is invalid, skipping...");
    return true;
  }

  errno = 0;
  ioprio_set(IOPRIO_WHO_PROCESS, static_cast<id_t>(pid), io_prio);

  return test_errno(errno, __func__, pid);
}

bool set_sched(pid_t pid, const std::string_view &sched_name,
               unsigned rt_prio) {
  int         sched = 0;
  sched_param param = {};
  if (sched_name == "idle") {
    sched = SCHED_IDLE;
  } else if (sched_name == "normal" || sched_name == "other") {
    sched = SCHED_OTHER;
  } else if (sched_name == "rr") {
    sched = SCHED_RR;
    param.sched_priority = static_cast<int>(rt_prio);
  } else if (sched_name == "fifo") {
    sched = SCHED_FIFO;
    param.sched_priority = static_cast<int>(rt_prio);
  } else if (sched_name == "deadline") {
    spdlog::warn("deadline scheduler is not available yet (due to its "
                 "different behavior)");
    //    sched = SCHED_DEADLINE;
    //    param.sched_priority = 1;
  } else if (sched_name == "batch") {
    sched = SCHED_BATCH;
  } else {
    spdlog::error("Invalid scheduler '{}'", __func__, sched_name);
    return false;
  }
  errno = 0;
  sched_setscheduler(pid, sched, &param);

  const int success = test_errno(errno, __func__, pid);
  if (success == -1)
    spdlog::error("{}: Error info: sched_name: {}, pid: {}", __func__,
                  sched_name, pid);

  return success;
}

bool set_oom_score_adjust(pid_t pid, int value) {

  try {
    const fs::path oom_score_path(std::format("/proc/{}/oom_score_adj", pid));

    std::ofstream oom_score_file{oom_score_path};
    if (oom_score_file.fail()) {
      spdlog::error("{}: Couldn't set OOM score adjustment to {} for {}",
                    __func__, value, pid);
      return false;
    }

    oom_score_file << value;
  } catch (const fs::filesystem_error &e) {
    spdlog::error("{}: Unknown filesystem error: {} (path: {})", __func__,
                  e.what(), e.path1().string());
    return false;
  }

  return true;
}

std::string_view test_latnice_support() noexcept {
  pid_t pid{0};
  int latency_nice{-20};

  const int saved_latnice = get_latnice(pid);
  errno = 0;

  // Use this here instead of function,
  // to suppress errors from function call.
  struct sched_attr attr = {
    .size = sizeof(struct sched_attr),
    .sched_flags = SCHED_FLAG_LATENCY_NICE,
    .sched_latency_nice = latency_nice,
  };
  const int err = sched_setattr(pid, &attr, 0);
  bool is_supported{};
  if (err == 0 && errno == 0) {
    set_latnice(pid, saved_latnice);
    is_supported = true;
  }
  return (is_supported) ? "true" : "false";
}

} // namespace priority
