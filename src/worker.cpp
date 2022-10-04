//
// Created by aviallon on 20/04/2021.
//

#include "core/worker.hpp"
#include "core/cgroups.hpp"
#include "core/priority.hpp"
#include "utility/process_info.hpp"

#include <chrono>
#include <functional>

#include <spdlog/spdlog.h>

using namespace std::chrono_literals;

Worker::Worker(Rules *rules_, Config *config_,
               synchronized_queue<Process> *process_queue_)
    : rules(rules_), config(config_), process_queue(process_queue_) {
  spdlog::info("Worker initialized with {} rules", rules->size());
}

void Worker::work(const std::stop_token &stop_token) {
  std::optional<Process> proc;
  const bool             is_affected_by_cgroup_bug =
      control_groups::get_cgroup_version().version ==
      control_groups::cgroup_info::v2;
  while (!stop_token.stop_requested()) {
    while ((proc = process_queue->poll(500ms)).has_value()) {
      const auto &p = proc.value();
      const auto &rule = rules->get_rule(p.name);

      processed_count++;

      const bool is_realtime = process_info::is_realtime(p.pid);
      const bool is_kernel = process_info::is_kernel_process(p.pid);

      if (!rule.is_null()) {
        if (spdlog::should_log(spdlog::level::debug)) {
          spdlog::debug("Found rule for {}: {}", p.name, rule.dump());
        } else {
          spdlog::info("{}({})", p.name, p.pid);
        }

        try {
          if (config->apply_nice() && rule.contains("nice")) {
            const int &rule_nice = rule["nice"];
            spdlog::debug("Setting priority of {}({}) to {}", p.name, p.pid,
                          rule_nice);
            if (!priority::set_priority(p.pid, rule_nice))
              continue;
          }

          if (config->apply_sched() && rule.contains("sched")) {
            const auto &rt_prio = rule.contains("rtprio")
                                             ? static_cast<unsigned>(rule["rtprio"])
                                             : 1u;
            const std::string &rule_sched = rule["sched"];
            spdlog::debug("Setting scheduler of {}({}) to {}", p.name, p.pid,
                          rule_sched);
            if (!priority::set_sched(p.pid, rule_sched.c_str(), rt_prio))
              continue;
          }

          if (config->apply_ionice() && rule.contains("ioclass")) {
            const std::string &rule_ioclass = rule["ioclass"];
            spdlog::debug("Setting ioclass of {}({}) to {}", p.name, p.pid,
                          rule_ioclass);
            int io_val = 0;
            if (rule.contains("ionice") && rule["ionice"].is_number_integer()) {
              io_val = rule["ionice"];
            }
            if (!priority::set_io_priority(p.pid, rule_ioclass.c_str(), io_val))
              continue;
          }

          if (config->apply_oom_score_adj() && rule.contains("oom_score_adj")) {
            const int &rule_oom_score_adj = rule["oom_score_adj"];
            spdlog::debug("Setting OOM score adjustment of {}({}) to {}",
                          p.name, p.pid, rule_oom_score_adj);
            if (!priority::set_oom_score_adjust(p.pid, rule_oom_score_adj))
              continue;
          }

          if (is_realtime && config->cgroup_realtime_workaround() &&
              is_affected_by_cgroup_bug) {
            spdlog::debug("Cgroups are not compatible with realtime "
                          "scheduling for now (linux limitation)");
          } else if (config->apply_cgroups() && rule.contains("cgroup")) {
            const std::string &rule_cgroup = rule["cgroup"];
            spdlog::debug("Adding process {}({}) to cgroup {}", p.name, p.pid,
                          rule_cgroup);
            if (!control_groups::add_pid_to_cgroup(p.pid, rule_cgroup))
              continue;
          }
        } catch (const std::exception &e) {
          spdlog::critical("{}: unhandled exception: {} (comm: {}, pid: {})",
                           __func__, e.what(), p.name, p.pid);
        }
      }

      if (is_realtime && config->cgroup_realtime_workaround() &&
          is_affected_by_cgroup_bug) {
        spdlog::debug("Moving realtime process {}({}) to root cgroup", p.name,
                      p.pid);
        control_groups::add_pid_to_cgroup(p.pid, "");
      }
    }
  }
}

void Worker::start() {
  worker_thread = std::jthread(std::bind_front(&Worker::work, this));
}
