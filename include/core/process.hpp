//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_PROCESS_HPP
#define ANANICY_CPP_PROCESS_HPP

#include "utility/utils.hpp"
#include <string>
#include <thread>

pid_t get_pid();

std::string get_command_from_pid(pid_t pid);

struct Process {
  pid_t       pid;
  std::string name;
};

class ProcessQueue {
private:
  struct Socket;

  int init();

  std::unique_ptr<Socket> sock;
  std::jthread            event_thread;

public:
  ProcessQueue();
  ~ProcessQueue();

  void start();
  void stop();

  void full_scan();

  atomic_queue<Process> process_queue;
};

#endif // ANANICY_CPP_PROCESS_HPP
