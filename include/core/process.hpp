//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_PROCESS_HPP
#define ANANICY_CPP_PROCESS_HPP

#include "utility/synchronized_queue.hpp"
#include "utility/utils.hpp"
#include <string>
#include <thread>

pid_t get_pid();

std::string get_command_from_pid(pid_t pid);

struct Process {
  pid_t       pid{};
  std::string name{};

  constexpr inline bool operator==(const Process &other) const {
    return (pid == other.pid) && (name == other.name);
  }
  constexpr inline bool operator!=(const Process &other) const {
    return !(*this == other);
  }
};

class ProcessQueue {
private:
  struct Socket;

  int init() noexcept(false);

  std::unique_ptr<Socket> sock;
  std::jthread            event_thread;

public:
  ProcessQueue();
  ~ProcessQueue() noexcept;

  void start();
  void stop() noexcept;

  void full_scan();

#ifdef USE_EXPERIMENTAL_IMPL
  synchronized_queue<Process> process_queue{INT16_MAX};
#else
  synchronized_queue<Process> process_queue;
#endif
};

#endif // ANANICY_CPP_PROCESS_HPP
