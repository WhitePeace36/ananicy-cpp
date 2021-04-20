//
// Created by aviallon on 20/04/2021.
//

#include <spdlog/spdlog.h>
#include "worker.hpp"
#include "priority.hpp"
#include <chrono>
using namespace std::chrono_literals;


Worker::Worker(InterruptHandler *interruptHandler, Rules *rules, Config* config, atomic_queue<Process>* process_queue) :
    interruptHandler(interruptHandler), rules(rules), config(config), process_queue(process_queue) {
    spdlog::info("Worker initialized with {} rules", rules->size());
}

void Worker::start() {
    std::function<void()> worker(
            [this]()  {
                while (!interruptHandler->should_exit()) {
                    interruptHandler->wait_for(10ms);
                    if (interruptHandler->should_exit())
                        break;

                    while (process_queue->size() > 0) {
                        Process p = process_queue->pop();
                        json rule = rules->get_rule(p.name);

                        processed_count++;

                        if (!rule.is_null()) {
                            spdlog::debug("Found rule for {}: {}", p.name, rule.dump());

                            if (config->apply_nice() && rule.contains("nice")) {
                                spdlog::info("Setting priority of {}({}) to {}", p.name, p.pid, to_string(rule["nice"]));
                                set_priority(p.pid, rule["nice"]);
                            }

                            if (config->apply_sched() && rule.contains("sched")) {
                                spdlog::info("Setting scheduler of {}({}) to {}", p.name, p.pid, to_string(rule["sched"]));
                                set_sched(p.pid, rule["sched"]);
                            }

                            if (config->apply_ionice() && rule.contains("ioclass")) {
                                spdlog::info("Setting ioclass of {}({}) to {}", p.name, p.pid, to_string(rule["ioclass"]));
                                int io_val = 0;
                                if (rule.contains("ionice") && rule["ionice"].is_number_integer()) {
                                    io_val = rule["ionice"];
                                }
                                set_io_priority(p.pid, rule["ioclass"], io_val);
                            }


                        }

                    }
                }
            });

    worker_thread = std::jthread(worker);
}
