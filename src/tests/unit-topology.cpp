#include "doctest_compatibility.h"

#include "core/cpuset.hpp"
#include "core/topology.hpp"

#include <set>

#include <spdlog/spdlog.h>

TEST_SUITE_BEGIN("topology");

TEST_CASE("detect produces valid topology") {
  auto topo = topology::detect();
  CHECK(!topo.cpus.empty());
  CHECK(!topo.llcs.empty());
  CHECK(!topo.nodes.empty());
  CHECK(!topo.all_cores_str.empty());
}

TEST_CASE("all online CPUs have valid capacity") {
  auto topo = topology::detect();
  for (const auto &cpu : topo.cpus) {
    if (cpu.online) {
      CHECK(cpu.cpu_capacity > 0);
      CHECK(cpu.cpu_capacity <= 1024);
    }
  }
}

TEST_CASE("cpuset strings are parseable") {
  auto topo = topology::detect();

  if (!topo.big_cores_str.empty()) {
    auto parsed = cpuset::parse_cpuset_string(topo.big_cores_str);
    CHECK(parsed.has_value());
  }

  if (!topo.little_cores_str.empty()) {
    auto parsed = cpuset::parse_cpuset_string(topo.little_cores_str);
    CHECK(parsed.has_value());
  }

  if (!topo.all_cores_str.empty()) {
    auto parsed = cpuset::parse_cpuset_string(topo.all_cores_str);
    CHECK(parsed.has_value());
  }
}

TEST_CASE("all_cores covers every online CPU") {
  auto topo = topology::detect();
  auto parsed = cpuset::parse_cpuset_string(topo.all_cores_str);
  REQUIRE(parsed.has_value());

  int online_count = 0;
  for (const auto &cpu : topo.cpus) {
    if (cpu.online) {
      CHECK(parsed->is_set(cpu.id));
      online_count++;
    }
  }
  CHECK(online_count > 0);
}

TEST_CASE("on homogeneous system all cores are Big") {
  auto topo = topology::detect();
  if (!topo.has_big_little) {
    CHECK(topo.little_cores_str.empty());
    CHECK(topo.big_cores_str == topo.all_cores_str);
  }
}

TEST_CASE("LLC grouping covers all online CPUs") {
  auto topo = topology::detect();
  std::set<int> llc_cpu_ids;
  for (const auto &llc : topo.llcs) {
    for (int id : llc.cpu_ids) {
      llc_cpu_ids.insert(id);
    }
  }
  for (const auto &cpu : topo.cpus) {
    if (cpu.online) {
      CHECK(llc_cpu_ids.count(cpu.id) == 1);
    }
  }
}

TEST_CASE("NUMA grouping covers all online CPUs") {
  auto topo = topology::detect();
  std::set<int> node_cpu_ids;
  for (const auto &node : topo.nodes) {
    for (int id : node.cpu_ids) {
      node_cpu_ids.insert(id);
    }
  }
  for (const auto &cpu : topo.cpus) {
    if (cpu.online) {
      CHECK(node_cpu_ids.count(cpu.id) == 1);
    }
  }
}

TEST_CASE("cpus_by_capacity sorted descending") {
  auto topo = topology::detect();
  for (std::size_t i = 1; i < topo.cpus_by_capacity.size(); ++i) {
    int prev = topo.cpus_by_capacity[i - 1];
    int curr = topo.cpus_by_capacity[i];
    CHECK(topo.cpus[prev].cpu_capacity >= topo.cpus[curr].cpu_capacity);
  }
}

TEST_SUITE_END();

