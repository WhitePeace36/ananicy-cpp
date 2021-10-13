#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> class synchronized_queue {
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

  template <typename Rep, typename Period>
  bool wait_for(std::chrono::duration<Rep, Period> timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cond_not_empty.wait_for(lock, timeout,
                                     [this] { return !m_queue.empty(); });
  }

  size_t size() { return m_queue.size(); }

  synchronized_queue() = default;

private:
  std::queue<T>           m_queue{};
  std::mutex              m_mutex;
  std::condition_variable m_cond_not_empty{};
};
