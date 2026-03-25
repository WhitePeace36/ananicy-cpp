#pragma once

#include <cstdint> // for int32_t, uint8_t
#include <string>  // for string
#include <vector>  // for vector

namespace topology {

enum class CoreType : std::uint8_t { BigTurbo, Big, Little };

struct CpuInfo {
  std::int32_t id;
  // topology/core_id
  std::int32_t core_id;
  // topology/physical_package_id
  std::int32_t package_id;
  // topology/cluster_id (-1 if N/A)
  std::int32_t cluster_id;
  // NUMA node
  std::int32_t node_id;
  // L3 cache domain (L2 fallback)
  std::int32_t llc_id;
  // cpufreq/scaling_min_freq (kHz)
  std::size_t min_freq;
  // cpufreq/scaling_max_freq (kHz)
  std::size_t max_freq;
  // cpufreq/base_frequency (kHz), fallback max_freq
  std::size_t base_freq;
  // normalized 0-1024
  std::size_t cpu_capacity;
  // per-CPU cache total (bytes)
  std::size_t cache_size;
  CoreType    core_type;
  bool        online;
};

struct LlcInfo {
  std::int32_t              id;
  std::vector<std::int32_t> cpu_ids;
  // precomputed
  std::string cpuset_str;
  // raw L3 cache size (bytes), 0 if no L3
  std::size_t l3_size;
};

struct NodeInfo {
  std::int32_t              id;
  std::vector<std::int32_t> cpu_ids;
  std::string               cpuset_str;
};

struct CpuTopology {
  std::vector<CpuInfo>  cpus;
  std::vector<LlcInfo>  llcs;
  std::vector<NodeInfo> nodes;
  bool                  smt_enabled;
  // max_cap >= 1.3 * min_cap
  bool has_big_little;

  std::string big_cores_str;
  std::string little_cores_str;
  std::string turbo_cores_str;
  std::string all_cores_str;
  // CPUs on the LLC(s) with the largest L3
  std::string               biggest_llc_cores_str;
  std::vector<std::int32_t> cpus_by_capacity;
};

[[nodiscard]] auto detect() noexcept -> CpuTopology;

} // namespace topology
