//
// Created by aviallon on 19/04/2021.
//

#ifndef ANANICY_CPP_RULES_HPP
#define ANANICY_CPP_RULES_HPP

#include "config.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

#ifdef ENABLE_REGEX_SUPPORT
#include <utility>
#include <vector>
#endif

#include <nlohmann/json.hpp>

class Rules {
private:
  using ruleset = std::unordered_map<std::string, nlohmann::json>;

  Config *m_config;
  ruleset m_program_rules{};
  ruleset m_type_rules{};
  ruleset m_cgroup_rules{};

#ifdef ENABLE_REGEX_SUPPORT
  std::vector<std::pair<std::string, std::string>> m_regex_rules{};
#endif

public:
  explicit Rules(const std::filesystem::path &ruleset_directory,
                 Config                      *config_src);
  bool load_rule_from_string(const std::string_view &line);
  void load_rules_from_file(const std::filesystem::path &path);
  void load_rules_from_directory(const std::filesystem::path &dir_path);
  nlohmann::json get_rule(const std::string &name) const noexcept;

  void create_cgroups() const noexcept;

  size_t size() const noexcept;

  void show_all_rules() const noexcept;

  enum class rule_type { rules, types, cgroups };
  void show_rules(rule_type type) const noexcept;
};

#endif // ANANICY_CPP_RULES_HPP
