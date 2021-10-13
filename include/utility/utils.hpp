//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_UTILS_HPP
#define ANANICY_CPP_UTILS_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>

class InterruptHandler {
private:
  std::condition_variable    exit_condition;
  std::mutex                 exit_mutex;
  volatile std::atomic<bool> need_exit;

public:
  InterruptHandler(std::initializer_list<int> signals);

  template <class Rep, class Period>
  inline void wait_for(std::chrono::duration<Rep, Period> duration) {
    std::unique_lock<std::mutex> lock(exit_mutex);
    exit_condition.wait_for(lock, duration);
  }

  [[nodiscard]] bool should_exit() const;
  void               stop();
};

std::optional<std::string> get_env(const std::string &name);

std::string get_error_string(int error_number);

std::string read_file(const std::string &path);

#endif // ANANICY_CPP_UTILS_HPP
