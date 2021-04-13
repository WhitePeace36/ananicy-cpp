//
// Created by aviallon on 13/04/2021.
//

#ifndef ANANICY_CPP_UTILS_HPP
#define ANANICY_CPP_UTILS_HPP

#include <atomic>
#include <queue>
#include <mutex>
#include <unistd.h>

template <typename T>
class atomic_queue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(value);
    }

    T pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        T front = m_queue.front();
        m_queue.pop();
        return front;
    }

    size_t size() {
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
};

class InterruptHandler {
private:
    volatile std::atomic<bool> need_exit;
public:
    InterruptHandler(std::initializer_list<int> signals);

    volatile std::atomic<bool> * should_exit();
};

#endif //ANANICY_CPP_UTILS_HPP
