#ifndef ANANICY_CPP_REGEX_UTILS_HPP
#define ANANICY_CPP_REGEX_UTILS_HPP

#include <cstdint>
#include <string_view>

namespace utils::regex {
bool is_str_matching_regex(const std::string_view &matching_str,
                           const std::string_view &regex_str) noexcept;
} // namespace utils::regex

#endif // ANANICY_CPP_REGEX_UTILS_HPP
