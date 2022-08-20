//
// Created by aviallon on 13/04/2021.
//
#include "utility/utils.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <functional>
bool InterruptHandler::should_exit() const { return this->need_exit; }

static std::function<void(int)> handler;
void                            on_signal(int unused) { handler(unused); }

InterruptHandler::InterruptHandler(std::initializer_list<int> signals) {

  handler = [this]([[maybe_unused]] int unused) -> void { this->stop(); };

  for (const int &sig : signals) {
    signal(sig, &on_signal);
  }
}

void InterruptHandler::stop() {
  need_exit = true;
  exit_condition.notify_all();
}

std::optional<std::string> get_env(const std::string &name) {
  char *res = std::getenv(name.c_str());
  if (res != nullptr) {
    return std::string(res);
  }
  return {};
}

std::string get_error_string(int error_number) {
  std::string msg{strerror(error_number)};
  return msg;
}

std::string read_file(const std::string &path) {
  constexpr std::size_t read_size = 4096;
  auto                  file_stream = std::ifstream(path);
  file_stream.exceptions(std::ios_base::badbit);

  std::string data;
  std::string buffer(read_size, '\0');
  while (file_stream.read(&buffer[0], read_size)) {
    data.append(buffer, 0, static_cast<size_t>(file_stream.gcount()));
  }
  data.append(buffer, 0, static_cast<size_t>(file_stream.gcount()));

  return data;
}
