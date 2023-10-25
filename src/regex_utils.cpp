#include "utility/regex_utils.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <string_view>

#include <spdlog/spdlog.h>

namespace {

bool generate_regex(pcre2_code            **regex,
                    const std::string_view &value) noexcept {
  if (regex == nullptr) {
    return false;
  }

  std::int32_t errorcode{};
  PCRE2_SIZE   offset{};

  *regex = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(value.data()),
                         PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP,
                         &errorcode, &offset, nullptr);
  if (!*regex) {
    std::array<PCRE2_UCHAR, 256> buffer{};
    pcre2_get_error_message(errorcode, buffer.data(), buffer.size());

    spdlog::error("regex compilation for '{}' failed: {}", errorcode,
                  std::string_view{reinterpret_cast<char *>(buffer.data())});
    return false;
  }

  return true;
}

std::int32_t regex_cmp(const std::string_view &item,
                       const pcre2_code       *regex) noexcept {
  pcre2_match_data *match_data =
      pcre2_match_data_create_from_pattern(regex, nullptr);
  const std::int32_t result =
      pcre2_match(regex, reinterpret_cast<PCRE2_SPTR>(item.data()), item.size(),
                  0, 0, match_data, nullptr);
  pcre2_match_data_free(match_data);
  return result;
}

} // namespace

namespace utils::regex {

bool is_str_matching_regex(const std::string_view &matching_str,
                           const std::string_view &regex_str) noexcept {
  pcre2_code *regex = nullptr;
  if (!generate_regex(&regex, regex_str)) {
    spdlog::error("broken regex: '{}'", regex_str);
    return false;
  }

  const std::int32_t re_res = regex_cmp(matching_str, regex);
  if (re_res < 0) {
    switch (re_res) {
    case PCRE2_ERROR_NOMATCH:
      break;
    default:
      spdlog::trace("matching error: {}", re_res);
      break;
    }
    pcre2_code_free(regex);
    return false;
  }

  pcre2_code_free(regex);
  return true;
}

} // namespace utils::regex
