#include "utility/backtrace.hpp"

#include <cxxabi.h>
#include <execinfo.h>
#include <format>
#include <iostream>
#include <cstdlib>
#include <stdexcept>

static constexpr int BACKTRACE_LENGTH = 20;

namespace backtrace_handler {

void print_trace() {
  void* backtrace_buffer[BACKTRACE_LENGTH];
  int    size = backtrace(backtrace_buffer, BACKTRACE_LENGTH);
  char** lines = backtrace_symbols(backtrace_buffer, size);
  if (lines != nullptr) {
    std::cerr << std::format("Obtained {} stack frames.\n", size);
    for (int i = 0; i < size; ++i) {
      std::cerr << std::format("({}) {}\n", i, lines[i]);
    }
  }

  // NOTE: You have to call here `free` function directly
  // because of delete/free their allocators does not
  // have exactly same behaviour
  free(lines);
}

[[noreturn]]
static void terminate_handler() {
  [[maybe_unused]] static int tried_throw = 0;

  try {
    if (tried_throw++ == 0)
      throw;
  } catch (const std::exception &e) {
    std::cerr << std::format("{} caught unhandled exception: {} {}",
                             __PRETTY_FUNCTION__,
                             abi::__cxa_demangle(typeid(e).name(), nullptr,
                                                 nullptr, nullptr),
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

[[maybe_unused]] static const bool set_terminate =
    std::set_terminate(terminate_handler);

} // namespace backtrace_handler
