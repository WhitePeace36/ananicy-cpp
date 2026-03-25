#pragma once

#include <sched.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cpuset {

/// Number of bits in a CPU bitmask on current system
auto get_max_number_of_cpus() noexcept -> std::int32_t;
/// Number of configured cores
auto get_num_conf_cores() noexcept -> std::uint64_t;
/// Number of online cores
auto get_num_online_cores() noexcept -> std::size_t;

/// RAII wrapper for cpu_set_t
struct CpuSet {
  cpu_set_t   *set{nullptr};
  std::size_t  setsize{0};
  std::int32_t ncpus{0};

  explicit CpuSet(std::int32_t num_cpus);
  ~CpuSet();

  CpuSet(const CpuSet &) = delete;
  CpuSet &operator=(const CpuSet &) = delete;

  CpuSet(CpuSet &&other) noexcept;
  CpuSet &operator=(CpuSet &&other) noexcept;

  void               set_cpu(std::int32_t cpu) const;
  void               clear_cpu(std::int32_t cpu) const;
  [[nodiscard]] bool is_set(std::int32_t cpu) const;
  void               zero() const;

  [[nodiscard]] bool valid() const { return set != nullptr; }
};

/// Parse Linux cpuset notation (e.g., "0-7", "0,1,2,3", "0-3,8-11")
/// Returns nullopt on parse failure
auto parse_cpuset_string(const std::string_view &cpuset_str,
                         std::int32_t max_cpus = 0) -> std::optional<CpuSet>;

/// Apply cpuset affinity to all threads of a process
bool set_affinity(pid_t pid, const CpuSet &cpuset);

/// Serialize a CpuSet to a human-readable string (e.g., "0-3,8-11")
auto cpuset_to_string(const CpuSet &cpuset) -> std::string;

} // namespace cpuset
