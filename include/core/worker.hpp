//
// Created by aviallon on 20/04/2021.
//

#pragma once

#include "config.hpp"
#include "process.hpp"
#include "rules.hpp"
#include "topology.hpp"
#include "utility/synchronized_queue.hpp"
#include "utility/utils.hpp"
#include "x3d.hpp"

#include <optional>
#include <thread>
#include <unordered_map>

class Worker {
public:
  Worker(Rules *rules, Config *config,
         synchronized_queue<Process>    *process_queue,
         topology::CpuTopology           topology,
         std::optional<x3d::X3DTopology> x3d_topology = std::nullopt);
  void start();
  void stop() {
    worker_thread.request_stop();
    worker_thread.join();
  }

  [[nodiscard]] unsigned processed_processes() const { return processed_count; }

private:
  Rules                                       *rules;
  Config                                      *config;
  std::jthread                                 worker_thread;
  synchronized_queue<Process>                 *process_queue;
  std::atomic<unsigned>                        processed_count;
  topology::CpuTopology                        m_topology;
  std::optional<x3d::X3DTopology>              m_x3d_topology;
  std::unordered_map<std::string, std::string> m_cpuset_aliases;

  void build_cpuset_aliases();
  void work(const std::stop_token &stop_token);
};
