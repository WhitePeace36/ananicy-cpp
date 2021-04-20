//
// Created by aviallon on 20/04/2021.
//

#include <spdlog/spdlog.h>
#include "worker.hpp"
#include "priority.hpp"
#include <chrono>
using namespace std::chrono_literals;


Worker::Worker(InterruptHandler *interruptHandler, Rules *rules, atomic_queue<Process>* process_queue) :
    interruptHandler(interruptHandler), rules(rules), process_queue(process_queue) {
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
                            spdlog::debug("Rule for {}: {}", p.name, rules->get_rule(p.name).dump());

                            if (rule.contains("nice")) {
                                spdlog::info("Setting priority of {}({}) to {}", p.name, p.pid, to_string(rule["nice"]));
                                set_priority(p.pid, rule["nice"]);
                            }
                        }

                    }
                }
            });

    worker_thread = std::jthread(worker);
}
