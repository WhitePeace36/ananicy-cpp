//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_UTILS_HPP
#define ANANICY_CPP_UTILS_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> class atomic_queue {
public:
  void push(const T &value) {

    std::lock_guard<std::mutex> lock(m_mutex);

    m_queue.push(value);
    m_cond_not_empty.notify_one();
  }

  T pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond_not_empty.wait(lock, [this] { return !m_queue.empty(); });

    T front = m_queue.front();
    m_queue.pop();
    return front;
  }

  template <typename Rep, typename Period>
  std::optional<T> poll(std::chrono::duration<Rep, Period> timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_cond_not_empty.wait_for(lock, timeout,
                                  [this] { return !m_queue.empty(); })) {
      if (m_queue.empty()) {
        return {};
      }

      T front = m_queue.front();
      m_queue.pop();
      T value = front;

      return value;
    }
    return {};
  }

  size_t size() { return m_queue.size(); }

private:
  std::queue<T>           m_queue;
  std::mutex              m_mutex;
  std::condition_variable m_cond_not_empty;
};

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
