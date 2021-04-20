//
// Created by aviallon on 20/04/2021.
//

#ifndef ANANICY_CPP_WORKER_HPP
#define ANANICY_CPP_WORKER_HPP

#include <thread>
#include "utils.hpp"
#include "rules.hpp"
#include "process_listener.hpp"
#include "config.hpp"

class Worker {
public:
    Worker(InterruptHandler* interruptHandler, Rules* rules, Config* config, atomic_queue<Process>* process_queue);
    void start();

    [[nodiscard]] unsigned processed_processes() const { return processed_count; }
private:
    InterruptHandler* interruptHandler;
    Rules* rules;
    Config* config;
    std::jthread worker_thread;
    atomic_queue<Process>* process_queue;
    std::atomic<unsigned> processed_count;


};

#endif //ANANICY_CPP_WORKER_HPP
