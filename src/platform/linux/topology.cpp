#include "core/topology.hpp"
#include "core/cpuset.hpp"
#include "utility/sysfs.hpp"

#include <algorithm>   // for max_element, sort
#include <filesystem>  // for directory_iterator
#include <limits>      // for numeric_limits
#include <map>         // for map
#include <string>      // for string
#include <string_view> // for string_view
#include <vector>      // for vector

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

// Linux kernel SCHED_CAPACITY_SCALE
constexpr std::size_t CAPACITY_SCALE = 1024;
// big.LITTLE threshold: max capacity must exceed min by this factor
constexpr float BIG_LITTLE_RATIO = 1.3f;

// Ordered from most precise to least precise
constexpr std::array SOURCES = {
    "cpufreq/amd_pstate_prefcore_ranking"sv,
    "cpufreq/amd_pstate_highest_perf"sv,
    "acpi_cppc/highest_perf"sv,
    "cpu_capacity"sv,
    "cpufreq/cpuinfo_max_freq"sv,
};

struct CapacitySource {
  std::string suffix;
  std::size_t avg_rcap;
  std::size_t max_rcap;
  bool        has_biglittle;
};

auto get_capacity_source(std::int32_t num_cpus) noexcept -> CapacitySource {
  // fallback
  auto chosen_suffix = SOURCES.back();

  for (auto &&source : SOURCES) {
    std::int32_t val0 = sysfs::read_int(sysfs::cpu_subpath(0, source), 0);
    if (val0 <= 0) {
      continue;
    }

    // Check if this source differentiates across CPUs
    chosen_suffix = source;
    bool differs = false;
    for (std::int32_t i = 1; i < num_cpus; ++i) {
      std::int32_t val_i = sysfs::read_int(sysfs::cpu_subpath(i, source), 0);
      if (val_i != val0) {
        differs = true;
        break;
      }
    }
    if (differs) {
      break;
    }
  }

  // Compute min/avg/max across all CPUs
  std::size_t  max_rcap{};
  std::size_t  min_rcap{std::numeric_limits<std::size_t>::max()};
  std::size_t  sum_rcap{};
  std::int32_t nr_cpus{};

  for (std::int32_t i = 0; i < num_cpus; ++i) {
    auto rcap = static_cast<std::size_t>(
        sysfs::read_int(sysfs::cpu_subpath(i, chosen_suffix), 0));
    // offline or unavailable
    if (rcap == 0) {
      continue;
    }

    max_rcap = std::max(max_rcap, rcap);
    min_rcap = std::min(min_rcap, rcap);
    sum_rcap += rcap;
    nr_cpus++;
  }

  if (nr_cpus == 0 || max_rcap == 0) {
    spdlog::warn("topology: CPU capacity information not available in sysfs");
    return CapacitySource{.suffix = "",
                          .avg_rcap = CAPACITY_SCALE,
                          .max_rcap = CAPACITY_SCALE,
                          .has_biglittle = false};
  }

  std::size_t avg_rcap = sum_rcap / nr_cpus;

  bool has_biglittle = static_cast<float>(max_rcap) >=
                       BIG_LITTLE_RATIO * static_cast<float>(min_rcap);

  spdlog::debug("topology: capacity source='{}' min={} avg={} max={} "
                "big.LITTLE={}",
                chosen_suffix, min_rcap, avg_rcap, max_rcap,
                has_biglittle ? "yes"sv : "no"sv);

  return CapacitySource{.suffix = std::string{chosen_suffix},
                        .avg_rcap = avg_rcap,
                        .max_rcap = max_rcap,
                        .has_biglittle = has_biglittle};
}

auto classify_core_type(std::size_t rcap, std::size_t avg_rcap,
                        std::size_t max_rcap, bool has_biglittle) noexcept
    -> topology::CoreType {
  if (has_biglittle && rcap == max_rcap && avg_rcap < max_rcap) {
    return topology::CoreType::BigTurbo;
  }
  if (!has_biglittle || rcap >= avg_rcap) {
    return topology::CoreType::Big;
  }
  return topology::CoreType::Little;
}

auto detect_smt() noexcept -> bool {
  return sysfs::read_int("/sys/devices/system/cpu/smt/active", 0) == 1;
}

/// Get NUMA node ID for a CPU via the kernel-provided nodeN symlink.
auto get_node_id(std::string_view base) noexcept -> std::int32_t {
  for (const auto &entry : fs::directory_iterator(base)) {
    auto name = entry.path().filename().string();
    if (name.starts_with("node"sv) && name.size() > 4) {
      return std::stoi(name.substr(4));
    }
  }
  return 0;
}

// L3 → L2 fallback for LLC grouping
auto get_llc_id(std::string_view                     base,
                std::map<std::string, std::int32_t> &llc_map) noexcept
    -> std::int32_t {
  for (std::int32_t level = 3; level >= 2; --level) {
    auto path = fmt::format("{}/cache/index{}/shared_cpu_list", base, level);
    auto key = sysfs::read_string(path);
    if (key.empty()) {
      continue;
    }

    auto it = llc_map.find(key);
    if (it != llc_map.end()) {
      return it->second;
    }

    auto new_id = static_cast<std::int32_t>(llc_map.size());
    llc_map[key] = new_id;
    return new_id;
  }
  // single unified cache
  return 0;
}

// Each cache level's size divided by the number of CPUs sharing it
auto get_cache_size(std::string_view base) noexcept -> std::size_t {
  std::size_t total = 0;
  for (std::int32_t idx = 0; idx < 8; ++idx) {
    auto index_base = fmt::format("{}/cache/index{}", base, idx);
    auto size_str = sysfs::read_string(fmt::format("{}/size", index_base));
    if (size_str.empty()) {
      break;
    }

    auto cpulist_str =
        sysfs::read_string(fmt::format("{}/shared_cpu_list", index_base));
    std::int32_t num_sharing = 1;
    if (!cpulist_str.empty()) {
      auto parsed = cpuset::parse_cpuset_string(cpulist_str);
      if (parsed.has_value()) {
        std::int32_t count = 0;
        for (std::int32_t c = 0; c < parsed->ncpus; ++c) {
          if (parsed->is_set(c)) {
            count++;
          }
        }
        if (count > 0) {
          num_sharing = count;
        }
      }
    }

    total += sysfs::parse_size_string(size_str) / num_sharing;
  }
  return total;
}

auto build_cpuset_string(const std::vector<std::int32_t> &cpu_ids) noexcept
    -> std::string {
  if (cpu_ids.empty()) {
    return "";
  }

  const auto    &max_cpu = *std::max_element(cpu_ids.begin(), cpu_ids.end());
  cpuset::CpuSet cpu_set(max_cpu + 1);
  if (!cpu_set.valid()) {
    return "";
  }

  for (auto &&id : cpu_ids) {
    cpu_set.set_cpu(id);
  }

  return cpuset::cpuset_to_string(cpu_set);
}

} // namespace

namespace topology {

auto detect() noexcept -> CpuTopology {
  CpuTopology topo{};

  const auto num_cpus = static_cast<std::int32_t>(cpuset::get_num_conf_cores());
  if (num_cpus <= 0) {
    spdlog::warn("topology: could not determine number of CPUs");
    return topo;
  }

  topo.smt_enabled = detect_smt();

  auto cap_src = get_capacity_source(num_cpus);
  topo.has_big_little = cap_src.has_biglittle;

  std::map<std::string, std::int32_t> llc_map;
  topo.cpus.resize(num_cpus);

  for (std::int32_t i = 0; i < num_cpus; ++i) {
    const auto base = sysfs::cpu_path(i);

    // cpu0 is always online
    bool online =
        (i == 0) || sysfs::read_int(fmt::format("{}/online", base), 1) == 1;

    auto &cpu = topo.cpus[i];
    cpu.id = i;
    cpu.online = online;

    if (!online) {
      cpu.core_type = CoreType::Big;
      cpu.cpu_capacity = 0;
      continue;
    }

    const auto top_path = fmt::format("{}/topology", base);
    cpu.core_id = sysfs::read_int(fmt::format("{}/core_id", top_path), 0);
    cpu.package_id =
        sysfs::read_int(fmt::format("{}/physical_package_id", top_path), 0);
    cpu.cluster_id =
        sysfs::read_int(fmt::format("{}/cluster_id", top_path), -1);

    cpu.node_id = get_node_id(base);
    cpu.llc_id = get_llc_id(base, llc_map);

    const auto freq_path = fmt::format("{}/cpufreq", base);
    cpu.min_freq = static_cast<std::size_t>(
        sysfs::read_int(fmt::format("{}/scaling_min_freq", freq_path), 0));
    cpu.max_freq = static_cast<std::size_t>(
        sysfs::read_int(fmt::format("{}/scaling_max_freq", freq_path), 0));
    cpu.base_freq = static_cast<std::size_t>(
        sysfs::read_int(fmt::format("{}/base_frequency", freq_path),
                        static_cast<std::int32_t>(cpu.max_freq)));

    std::size_t raw_cap = cap_src.max_rcap;
    if (!cap_src.suffix.empty()) {
      raw_cap = static_cast<std::size_t>(
          sysfs::read_int(sysfs::cpu_subpath(i, cap_src.suffix), 0));
    }
    cpu.cpu_capacity = cap_src.max_rcap > 0
                           ? (raw_cap * CAPACITY_SCALE) / cap_src.max_rcap
                           : CAPACITY_SCALE;

    cpu.cache_size = get_cache_size(base);
    cpu.core_type = classify_core_type(raw_cap, cap_src.avg_rcap,
                                       cap_src.max_rcap, cap_src.has_biglittle);
  }

  std::map<std::int32_t, std::vector<std::int32_t>> llc_groups;
  std::map<std::int32_t, std::vector<std::int32_t>> node_groups;
  std::vector<std::int32_t>                         big_cpus;
  std::vector<std::int32_t>                         little_cpus;
  std::vector<std::int32_t>                         turbo_cpus;
  std::vector<std::int32_t>                         all_cpus;

  for (const auto &cpu : topo.cpus) {
    if (!cpu.online) {
      continue;
    }

    all_cpus.push_back(cpu.id);
    llc_groups[cpu.llc_id].push_back(cpu.id);
    node_groups[cpu.node_id].push_back(cpu.id);

    switch (cpu.core_type) {
    case CoreType::BigTurbo:
      big_cpus.push_back(cpu.id);
      turbo_cpus.push_back(cpu.id);
      break;
    case CoreType::Big:
      big_cpus.push_back(cpu.id);
      break;
    case CoreType::Little:
      little_cpus.push_back(cpu.id);
      break;
    }
  }

  for (const auto &[llc_id, cpus] : llc_groups) {
    auto l3 = sysfs::read_cpu_l3_size(cpus.front());
    topo.llcs.emplace_back(llc_id, cpus, build_cpuset_string(cpus), l3);
  }

  // Find LLC(s) with the largest L3 and collect their CPUs
  std::size_t max_l3 = 0;
  for (const auto &llc : topo.llcs) {
    max_l3 = std::max(max_l3, llc.l3_size);
  }
  if (max_l3 > 0) {
    std::vector<std::int32_t> biggest_llc_cpus;
    for (const auto &llc : topo.llcs) {
      if (llc.l3_size == max_l3) {
        biggest_llc_cpus.insert(biggest_llc_cpus.end(), llc.cpu_ids.begin(),
                                llc.cpu_ids.end());
      }
    }
    topo.biggest_llc_cores_str = build_cpuset_string(biggest_llc_cpus);
  }

  for (const auto &[node_id, cpus] : node_groups) {
    topo.nodes.emplace_back(node_id, cpus, build_cpuset_string(cpus));
  }

  topo.big_cores_str = build_cpuset_string(big_cpus);
  topo.little_cores_str = build_cpuset_string(little_cpus);
  topo.turbo_cores_str = build_cpuset_string(turbo_cpus);
  topo.all_cores_str = build_cpuset_string(all_cpus);

  topo.cpus_by_capacity = all_cpus;
  std::sort(topo.cpus_by_capacity.begin(), topo.cpus_by_capacity.end(),
            [&topo](auto &&a, auto &&b) {
              return topo.cpus[a].cpu_capacity > topo.cpus[b].cpu_capacity;
            });

  spdlog::info("topology: {} CPUs, {} LLCs, {} NUMA nodes, SMT={}, "
               "big.LITTLE={}",
               all_cpus.size(), topo.llcs.size(), topo.nodes.size(),
               topo.smt_enabled ? "on" : "off",
               topo.has_big_little ? "yes" : "no");

  return topo;
}

} // namespace topology
