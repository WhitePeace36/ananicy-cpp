#pragma once

#include <cstdint>     // for int32_t
#include <filesystem>  // for path
#include <string>      // for string
#include <string_view> // for string_view

namespace sysfs {

/// Read an integer from a sysfs file, returns fallback on failure
[[nodiscard]] auto read_int(const std::filesystem::path &path,
                            std::int32_t                 fallback = -1) noexcept
    -> std::int32_t;

/// Read a trimmed string from a sysfs file, returns "" on failure
[[nodiscard]] auto read_string(const std::filesystem::path &path) noexcept
    -> std::string;

/// Parse a size string like "98304K" or "32M" to bytes
[[nodiscard]] auto parse_size_string(std::string_view size) noexcept
    -> std::size_t;

/// Build sysfs path for a CPU: /sys/devices/system/cpu/cpuN
[[nodiscard]] auto cpu_path(std::int32_t cpu_id) noexcept -> std::string;

/// Build sysfs sub-path for a CPU: /sys/devices/system/cpu/cpuN/suffix
[[nodiscard]] auto cpu_subpath(std::int32_t     cpu_id,
                               std::string_view suffix) noexcept -> std::string;

/// Read L3 cache size in bytes for a given CPU (0 if no L3)
[[nodiscard]] auto read_cpu_l3_size(std::int32_t cpu_id) noexcept
    -> std::size_t;

} // namespace sysfs
