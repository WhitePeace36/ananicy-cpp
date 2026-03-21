#include "utility/mounts.hpp"

#include <cstdint>
#include <string_view>

// see http://llvm.org/docs/LibFuzzer.html
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  std::string_view data_str{reinterpret_cast<const char *>(data), size};
  mounts::parse_mtab_content(data_str);

  return 0;
}
