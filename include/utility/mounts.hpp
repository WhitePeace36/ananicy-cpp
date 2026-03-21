#pragma once

#include <optional>    // for optional
#include <string>      // for string
#include <string_view> // for string_view
#include <vector>      // for vector

namespace mounts {

struct MountEntry {
  std::string device{};
  std::string mountpoint{};
  std::string fs_type{};
};

// Parse mtab content
auto parse_mtab_content(std::string_view mtab_content) noexcept
    -> std::vector<mounts::MountEntry>;

// Parse mounts
auto parse_mounts() noexcept -> std::optional<std::vector<MountEntry>>;

} // namespace mounts
