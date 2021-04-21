//
// Created by aviallon on 13/04/2021.
//
#include <csignal>
#include <functional>
#include "utils.hpp"

#ifdef ENABLE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

bool InterruptHandler::should_exit() const {
    return this->need_exit;
}

static std::function<void(int)> handler;
void on_signal (int unused) {
    handler(unused);
}

InterruptHandler::InterruptHandler(std::initializer_list<int> signals) {

    handler = [this](int unused) -> void {
        this->need_exit = true;
        this->exit_condition.notify_all();

#ifdef ENABLE_SYSTEMD
        sd_notify(0, "STOPPING=1");
#endif
    };

    for (const int& sig : signals) {
        signal (sig, &on_signal);
    }
}