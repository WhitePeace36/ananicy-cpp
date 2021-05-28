#include "backtrace.hpp"
#include <cxxabi.h>
#include <execinfo.h>
#include <format>
#include <iostream>
#include <stdexcept>

#define BACKTRACE_LENGTH 10

namespace backtrace_handler {
[[maybe_unused]] static const bool set_terminate =
    std::set_terminate(terminate_handler);

void print_trace() {
  void * backtrace_buffer[10];
  char **lines;
  int    size = backtrace(backtrace_buffer, BACKTRACE_LENGTH);
  lines = backtrace_symbols(backtrace_buffer, size);
  if (lines != nullptr) {
    std::cerr << std::format("Obtained {} stack frames.\n", size);
    for (unsigned int i = 0; i < size; i++) {
      std::cerr << std::format("({}) {}\n", i, lines[i]);
    }
  }
  delete[] lines;
}

void terminate_handler() {
  static int tried_throw = 0;

  try {
    if (tried_throw++ == 0)
      throw;
  } catch (const std::exception &e) {
    std::cerr << std::format(
                     "{} caught unhandled exception: {} {}",
                     __PRETTY_FUNCTION__,
                     abi::__cxa_demangle(typeid(e).name(), 0, 0, nullptr),
                     e.what())
              << std::endl;
  } catch (...) {
    std::cerr << std::format("{} caught unknown/unhandled exception.",
                             __PRETTY_FUNCTION__)
              << std::endl;
  }

  print_trace();

  abort();
}
} // namespace backtrace_handler