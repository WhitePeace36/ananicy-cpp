#include "utility/mounts.hpp"

#include <filesystem>
#include <fstream>
#include <iterator> // For istreambuf_iterator
#include <optional>
#include <string>
#include <string_view>
#include <utility> // For move
#include <vector>

namespace fs = std::filesystem;

namespace {

// Helper function to split a string_view by a delimiter
auto split_string_view(std::string_view str, char delim) noexcept
    -> std::vector<std::string_view> {
  std::vector<std::string_view> result;

  std::size_t start = 0;
  for (std::size_t i = 0; i < str.size(); ++i) {
    if (str[i] == delim) {
      if (start < i) {
        // Add non-empty segment
        result.push_back(str.substr(start, i - start));
      }
      start = i + 1;
    }
  }
  // Add the last segment if it's not empty or if the string was empty
  if (start < str.size()) {
    result.push_back(str.substr(start));
  }

  return result;
}

auto read_mtab(std::string_view mtab_path) noexcept
    -> std::optional<std::string> {
  // mtab file size is reported as zero bytes
  // so just read line by line until EOF
  std::ifstream file(fs::path{mtab_path}, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  // Use iterators to read the whole file efficiently
  std::string content((std::istreambuf_iterator<char>(file)),
                      (std::istreambuf_iterator<char>()));

  // Check for read errors after attempting to read
  if (file.bad()) {
    return std::nullopt;
  }

  return std::make_optional(std::move(content));
}

} // namespace

namespace mounts {

auto parse_mtab_content(std::string_view mtab_content) noexcept
    -> std::vector<mounts::MountEntry> {
  std::vector<mounts::MountEntry> entries{};

  const auto file_content_lines = split_string_view(mtab_content, '\n');
  for (const auto &line : file_content_lines) {
    // Skip empty lines or lines starting with '#'
    if (line.empty() || line.starts_with('#')) {
      continue;
    }

    const auto line_split = split_string_view(line, ' ');

    // Check if we have at least 3 fields (device, mountpoint, fstype)
    if (line_split.size() >= 3 && !line_split[0].starts_with('#')) {
      entries.emplace_back(
          mounts::MountEntry{.device = std::string{line_split[0]},
                             .mountpoint = std::string{line_split[1]},
                             .fs_type = std::string{line_split[2]}});
    }
  }
  return entries;
}

auto parse_mounts() noexcept -> std::optional<std::vector<MountEntry>> {
  // use "non-standard" read due to reported zero size
  auto file_content = read_mtab("/proc/self/mounts");
  if (!file_content || file_content->empty()) {
    return std::nullopt;
  }
  return parse_mtab_content(*file_content);
}

} // namespace mounts
