//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_PROCESS_HPP
#define ANANICY_CPP_PROCESS_HPP

#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <thread>
#include "utils.hpp"

struct Process {
    pid_t pid;
    std::string name;
};


class ProcessQueue {
private:
    struct Socket;

    int init();

    std::unique_ptr<Socket> sock;
    std::jthread event_thread;
    InterruptHandler* interruptHandler;

public:
    explicit ProcessQueue(InterruptHandler* interruptHandler);
    ~ProcessQueue();

    void start();

    atomic_queue<Process> process_queue;
};

#endif //ANANICY_CPP_PROCESS_HPP
