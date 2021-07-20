//
// Created by aviallon on 20/04/2021.
//

#include "core/worker.hpp"
#include "core/cgroups.hpp"
#include "core/priority.hpp"
#include <chrono>
#include <spdlog/spdlog.h>
using namespace std::chrono_literals;

Worker::Worker(Rules *rules, Config *config,
               atomic_queue<Process> *process_queue)
    : rules(rules), config(config), process_queue(process_queue) {
  spdlog::info("Worker initialized with {} rules", rules->size());
}

void Worker::start() {
  std::function<void(std::stop_token)> worker(
      [this, func_name = __func__](const std::stop_token &stop_token) {
        std::optional<Process> proc;
        while (!stop_token.stop_requested()) {
          while ((proc = process_queue->poll(500ms)).has_value()) {
            Process &p = proc.value();
            json     rule = rules->get_rule(p.name);

            processed_count++;

            if (!rule.is_null()) {
              if (spdlog::should_log(spdlog::level::debug)) {
                spdlog::debug("Found rule for {}: {}", p.name, rule.dump());
              } else {
                spdlog::info("{}({})", p.name, p.pid);
              }

              bool ok = true;

              try {
                if (config->apply_nice() && rule.contains("nice")) {
                  spdlog::debug("Setting priority of {}({}) to {}", p.name,
                                p.pid, to_string(rule["nice"]));
                  ok = set_priority(p.pid, rule["nice"]);
                }

                if (!ok)
                  continue;

                if (config->apply_sched() && rule.contains("sched")) {
                  unsigned rt_prio = rule.contains("rtprio")
                                         ? static_cast<unsigned>(rule["rtprio"])
                                         : 1u;
                  spdlog::debug("Setting scheduler of {}({}) to {}", p.name,
                                p.pid, to_string(rule["sched"]));
                  ok = set_sched(p.pid, rule["sched"], rt_prio);
                }

                if (!ok)
                  continue;

                if (config->apply_ionice() && rule.contains("ioclass")) {
                  spdlog::debug("Setting ioclass of {}({}) to {}", p.name,
                                p.pid, to_string(rule["ioclass"]));
                  int io_val = 0;
                  if (rule.contains("ionice") &&
                      rule["ionice"].is_number_integer()) {
                    io_val = rule["ionice"];
                  }
                  ok = set_io_priority(p.pid, rule["ioclass"], io_val);
                }

                if (!ok)
                  continue;

                if (config->apply_oom_score_adj() &&
                    rule.contains("oom_score_adj")) {
                  spdlog::debug("Setting OOM score adjustment of {}({}) to {}",
                                p.name, p.pid,
                                to_string(rule["oom_score_adj"]));
                  ok = set_oom_score_adjust(p.pid, rule["oom_score_adj"]);
                }

                if (config->apply_cgroups() && rule.contains("cgroup")) {
                  spdlog::debug("Adding process {}({}) to cgroup {}", p.name,
                                p.pid, to_string(rule["cgroup"]));
                  ok = control_groups::add_pid_to_cgroup(p.pid, rule["cgroup"]);
                }
              } catch (const std::exception &e) {
                spdlog::critical(
                    "{}: unhandled exception: {} (comm: {}, pid: {})",
                    func_name, e.what(), p.name, p.pid);
              }
            }
          }
        }
      });

  worker_thread = std::jthread(worker);
}
