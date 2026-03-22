#pragma once

#include <sched.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cpuset {

/// Number of bits in a CPU bitmask on current system
std::int32_t get_max_number_of_cpus() noexcept;
/// Number of configured cores
std::uint64_t get_num_conf_cores() noexcept;
/// Number of online cores
std::size_t get_num_online_cores() noexcept;

/// RAII wrapper for cpu_set_t
struct CpuSet {
  cpu_set_t  *set{nullptr};
  std::size_t setsize{0};
  int         ncpus{0};

  explicit CpuSet(int num_cpus);
  ~CpuSet();

  CpuSet(const CpuSet &) = delete;
  CpuSet &operator=(const CpuSet &) = delete;

  CpuSet(CpuSet &&other) noexcept;
  CpuSet &operator=(CpuSet &&other) noexcept;

  void set_cpu(int cpu) const;
  void clear_cpu(int cpu) const;
  [[nodiscard]] bool is_set(int cpu) const;
  void zero() const;

  [[nodiscard]] bool valid() const { return set != nullptr; }
};

/// Parse Linux cpuset notation (e.g., "0-7", "0,1,2,3", "0-3,8-11")
/// Returns nullopt on parse failure
std::optional<CpuSet> parse_cpuset_string(const std::string_view &cpuset_str, int max_cpus = 0);

/// Apply cpuset affinity to all threads of a process
bool set_affinity(pid_t pid, const CpuSet &cpuset);

/// Serialize a CpuSet to a human-readable string (e.g., "0-3,8-11")
std::string cpuset_to_string(const CpuSet &cpuset);

} // namespace cpuset
