//
// Created by aviallon on 13/04/2021.
//
#include <csignal>
#include <functional>
#include "utils.hpp"

volatile std::atomic<bool> *InterruptHandler::should_exit() {
    return &this->need_exit;
}

static std::function<void(int)> handler;
void on_signal (int unused) {
    handler(unused);
}

InterruptHandler::InterruptHandler(std::initializer_list<int> signals) {

    handler = [this](int unused) -> void {
        this->need_exit;
    };

    for (const int& sig : signals) {
        signal (sig, &on_signal);
    }
}
