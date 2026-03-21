#include "utility/sysfs.hpp"

#include <charconv>
#include <fstream>

#include <fmt/format.h>

namespace sysfs {

auto read_int(const std::filesystem::path &path, std::int32_t fallback) noexcept
    -> std::int32_t {
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return fallback;
    }

    std::int32_t val = fallback;
    file >> val;
    return val;
  } catch (...) {
    return fallback;
  }
}

auto read_string(const std::filesystem::path &path) noexcept -> std::string {
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return "";
    }

    std::string val{};
    std::getline(file, val);
    // Trim trailing whitespace/newlines
    while (!val.empty() &&
           (val.back() == ' ' || val.back() == '\n' || val.back() == '\r')) {
      val.pop_back();
    }
    return val;
  } catch (...) {
    return "";
  }
}

auto parse_size_string(std::string_view size) noexcept -> std::size_t {
  if (size.empty()) {
    return 0;
  }

  std::size_t val{};
  auto [ptr, ec] = std::from_chars(size.data(), size.data() + size.size(), val);
  if (ec != std::errc{}) {
    return 0;
  }

  if (ptr < size.data() + size.size()) {
    char suffix = *ptr;

    if (suffix == 'K' || suffix == 'k') {
      val *= 1024;
    } else if (suffix == 'M' || suffix == 'm') {
      val *= 1024ULL * 1024;
    } else if (suffix == 'G' || suffix == 'g') {
      val *= 1024ULL * 1024 * 1024;
    }
  }
  return val;
}

auto cpu_path(std::int32_t cpu_id) noexcept -> std::string {
  return fmt::format("/sys/devices/system/cpu/cpu{}", cpu_id);
}

auto cpu_subpath(std::int32_t cpu_id, std::string_view suffix) noexcept
    -> std::string {
  return fmt::format("/sys/devices/system/cpu/cpu{}/{}", cpu_id, suffix);
}

auto read_cpu_l3_size(std::int32_t cpu_id) noexcept -> std::size_t {
  return parse_size_string(
      read_string(cpu_subpath(cpu_id, "cache/index3/size")));
}

} // namespace sysfs
