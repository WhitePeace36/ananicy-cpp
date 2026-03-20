#include "core/cpuset.hpp"
#include "utility/utils.hpp"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>       // for cpu_set_t
#include <sys/syscall.h> // for SYS_sched_getaffinity
#include <unistd.h>      // for syscall

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

#define cpuset_nbits(setsize) (8 * (setsize))

namespace {

/// Allocates a new set for ncpus and returns size in bytes and size in bits
cpu_set_t *cpuset_alloc(int ncpus, size_t *setsize, size_t *nbits) noexcept {
  cpu_set_t *set = CPU_ALLOC(ncpus);

  if (!set) {
    return nullptr;
  }
  if (setsize) {
    *setsize = CPU_ALLOC_SIZE(ncpus);
  }
  if (nbits) {
    *nbits = cpuset_nbits(CPU_ALLOC_SIZE(ncpus));
  }
  return set;
}

void cpuset_free(cpu_set_t *set) noexcept { CPU_FREE(set); }

std::int32_t test_errno(std::int32_t errcode, const std::string_view &func_name,
                        pid_t pid) {
  if (errcode == 0) {
    spdlog::debug("{}: Successfully applied to {}", func_name, pid);
    return 1;
  }

  if (errcode == ESRCH) {
    spdlog::warn("{}: Process not found: {}", func_name, pid);
    return 0;
  } else if (errcode == EACCES || errcode == EPERM) {
    throw std::runtime_error(fmt::format(
        "{}: Insufficient permission to set cpu affinity of process {}",
        func_name, pid));
  }
  spdlog::error(fmt::format("{}: Unknown error, errno is {}({})", func_name,
                            std::string_view{strerror(errcode)}, errcode));
  return -1;
}

} // namespace

namespace cpuset {

std::int32_t get_max_number_of_cpus() noexcept {
#ifdef SYS_sched_getaffinity
  std::int32_t cpus{2048};
  std::size_t  setsize{};
  cpu_set_t   *set = cpuset_alloc(cpus, &setsize, nullptr);

  if (!set) {
    return -1; /* error */
  }

  for (;;) {
    CPU_ZERO_S(setsize, set);

    // the library version does not return size of cpumask_t
    const auto n = static_cast<std::int32_t>(
        syscall(SYS_sched_getaffinity, 0, setsize, set));

    if (n < 0 && errno == EINVAL && cpus < 1024 * 1024) {
      cpuset_free(set);
      cpus *= 2;
      set = cpuset_alloc(cpus, &setsize, nullptr);
      if (!set) {
        return -1; /* error */
      }
      continue;
    }
    cpuset_free(set);
    return n * 8;
  }
#endif
  return -1;
}

std::uint64_t get_num_conf_cores() noexcept {
  static const auto n =
      static_cast<std::uint64_t>(sysconf(_SC_NPROCESSORS_CONF));
  return n;
}

std::size_t get_num_online_cores() noexcept {
  const auto        ncores = get_num_conf_cores();
  cpu_set_t        *setp = CPU_ALLOC(ncores);
  const std::size_t setsize = CPU_ALLOC_SIZE(ncores);

  CPU_ZERO_S(setsize, setp);

  // get affinity of self
  if (sched_getaffinity(0, setsize, setp) == -1) {
    std::perror("sched_getaffinity(2) failed");
    std::exit(1);
  }

  std::size_t num_online{};
  for (std::size_t i = 0; i < CPU_COUNT_S(setsize, setp); ++i) {
    if (CPU_ISSET_S(i, setsize, setp)) {
      ++num_online;
    }
  }

  CPU_FREE(setp);
  return num_online;
}

CpuSet::CpuSet(int num_cpus) : ncpus(num_cpus) {
  set = CPU_ALLOC(ncpus);
  if (set) {
    setsize = CPU_ALLOC_SIZE(ncpus);
    CPU_ZERO_S(setsize, set);
  }
}

CpuSet::~CpuSet() {
  if (set) {
    CPU_FREE(set);
  }
}

CpuSet::CpuSet(CpuSet &&other) noexcept
    : set(other.set), setsize(other.setsize), ncpus(other.ncpus) {
  other.set = nullptr;
  other.setsize = 0;
  other.ncpus = 0;
}

CpuSet &CpuSet::operator=(CpuSet &&other) noexcept {
  if (this != &other) {
    if (set) {
      CPU_FREE(set);
    }
    set = other.set;
    setsize = other.setsize;
    ncpus = other.ncpus;
    other.set = nullptr;
    other.setsize = 0;
    other.ncpus = 0;
  }
  return *this;
}

void CpuSet::set_cpu(int cpu) const {
  if (set && cpu >= 0 && cpu < ncpus) {
    CPU_SET_S(cpu, setsize, set);
  }
}

void CpuSet::clear_cpu(int cpu) const {
  if (set && cpu >= 0 && cpu < ncpus) {
    CPU_CLR_S(cpu, setsize, set);
  }
}

bool CpuSet::is_set(int cpu) const {
  if (set && cpu >= 0 && cpu < ncpus) {
    return CPU_ISSET_S(cpu, setsize, set);
  }
  return false;
}

void CpuSet::zero() const {
  if (set) {
    CPU_ZERO_S(setsize, set);
  }
}

std::optional<CpuSet> parse_cpuset_string(const std::string_view &cpuset_str,
                                          int                     max_cpus) {
  if (cpuset_str.empty()) {
    return std::nullopt;
  }

  if (max_cpus <= 0) {
    max_cpus = static_cast<int>(get_num_conf_cores());
  }
  if (max_cpus <= 0) {
    return std::nullopt;
  }

  CpuSet result(max_cpus);
  if (!result.valid()) {
    return std::nullopt;
  }

  // Split by ',' and process each token
  std::size_t pos = 0;
  while (pos < cpuset_str.size()) {
    // Find next comma
    std::size_t comma = cpuset_str.find(',', pos);
    if (comma == std::string_view::npos) {
      comma = cpuset_str.size();
    }

    auto token = cpuset_str.substr(pos, comma - pos);
    if (token.empty()) {
      spdlog::warn("cpuset: empty token in '{}'", cpuset_str);
      return std::nullopt;
    }

    // Check for range (N-M)
    auto dash = token.find('-');
    if (dash != std::string_view::npos && dash > 0) {
      auto start_str = token.substr(0, dash);
      auto end_str = token.substr(dash + 1);

      if (start_str.empty() || end_str.empty()) {
        spdlog::warn("cpuset: invalid range token '{}' in '{}'", token,
                     cpuset_str);
        return std::nullopt;
      }

      const int start = to_int<int>(start_str);
      const int end = to_int<int>(end_str);

      // Validate: from_chars returns 0 on parse failure for these tokens
      // but we also need to check the range makes sense
      if (start < 0 || end < 0 || start > end || end >= max_cpus) {
        spdlog::warn("cpuset: invalid range {}-{} (max_cpus={})", start, end,
                     max_cpus);
        return std::nullopt;
      }

      for (int i = start; i <= end; ++i) {
        result.set_cpu(i);
      }
    } else if (dash == 0) {
      // Starts with '-', invalid (negative number)
      spdlog::warn("cpuset: negative value in '{}'", cpuset_str);
      return std::nullopt;
    } else {
      // Single CPU number
      if (std::any_of(token.begin(), token.end(),
                      [](unsigned char c) { return !std::isdigit(c); })) {
        spdlog::warn("cpuset: non-numeric token '{}' in '{}'", token,
                     cpuset_str);
        return std::nullopt;
      }

      const int cpu = to_int<int>(token);
      if (cpu < 0 || cpu >= max_cpus) {
        spdlog::warn("cpuset: cpu {} out of range (max_cpus={})", cpu,
                     max_cpus);
        return std::nullopt;
      }

      result.set_cpu(cpu);
    }

    pos = comma + 1;
  }

  return result;
}

bool set_affinity(pid_t pid, const CpuSet &cpuset) {
  if (!cpuset.valid()) {
    return false;
  }

  std::int32_t errcode = 0;
  const auto  &task_path = fmt::format("/proc/{}/task", pid);
  try {
    for (const auto &dir_entry : fs::directory_iterator(task_path)) {
      const auto &filename = dir_entry.path().filename().string();
      const auto  tid = to_int<std::int32_t>(filename);
      errno = 0;
      if (tid > 0) {
        sched_setaffinity(tid, cpuset.setsize, cpuset.set);
      }
      errcode = errno;
    }
  } catch (const fs::filesystem_error &e) {
    spdlog::debug(fmt::format("{}: filesystem_error: {} (path: {})", __func__,
                              e.what(), std::string_view{e.path1().c_str()}));
    return false;
  } catch (const std::exception &e) {
    spdlog::critical(
        fmt::format("{}: unknown exception: {}", __func__, e.what()));
  }

  return test_errno(errcode, __func__, pid);
}

std::string cpuset_to_string(const CpuSet &cpuset) {
  if (!cpuset.valid()) {
    return "";
  }

  std::string result;
  int         range_start = -1;
  int         range_end = -1;

  auto flush_range = [&]() {
    if (range_start < 0) {
      return;
    }
    if (!result.empty()) {
      result += ',';
    }
    if (range_start == range_end) {
      result += std::to_string(range_start);
    } else {
      result += fmt::format("{}-{}", range_start, range_end);
    }
  };

  for (int i = 0; i < cpuset.ncpus; ++i) {
    if (cpuset.is_set(i)) {
      if (range_start < 0) {
        range_start = i;
        range_end = i;
      } else {
        range_end = i;
      }
    } else {
      flush_range();
      range_start = -1;
    }
  }
  flush_range();

  return result;
}

} // namespace cpuset
