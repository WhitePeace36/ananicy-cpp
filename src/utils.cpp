//
// Created by aviallon on 13/04/2021.
//
#include "utils.hpp"
#include <csignal>
#include <functional>

#ifdef ENABLE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

bool InterruptHandler::should_exit() const { return this->need_exit; }

static std::function<void(int)> handler;
void                            on_signal(int unused) { handler(unused); }

InterruptHandler::InterruptHandler(std::initializer_list<int> signals) {

  handler = [this](int unused) -> void { this->stop(); };

  for (const int &sig : signals) {
    signal(sig, &on_signal);
  }
}

void InterruptHandler::stop() {
  need_exit = true;
  exit_condition.notify_all();

#ifdef ENABLE_SYSTEMD
  sd_notify(0, "STOPPING=1");
#endif
}

std::optional<std::string> get_env(const std::string &name) {
  char *res = std::getenv(name.c_str());
  if (res != nullptr) {
    return std::string(res);
  }
  return {};
}