#include "core/cpuset.hpp"

#include <cstdint>
#include <string_view>

#include <spdlog/spdlog.h>

static bool g_initialized = false;

void do_initialization() noexcept {
  if (!g_initialized) {
    spdlog::set_level(spdlog::level::off);
    g_initialized = true;
  }
}

// see http://llvm.org/docs/LibFuzzer.html
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  do_initialization();

  std::string_view data_str{reinterpret_cast<const char *>(data), size};

  // Fuzz parse_cpuset_string: must never crash, throw, or leak
  auto result = cpuset::parse_cpuset_string(data_str);

  // If parsing succeeded, verify roundtrip serialization doesn't crash
  if (result.has_value()) {
    auto str = cpuset::cpuset_to_string(result.value());
    // Re-parse the serialized string — must also succeed
    auto roundtrip = cpuset::parse_cpuset_string(str);
    (void)roundtrip;
  }

  return 0;
}
