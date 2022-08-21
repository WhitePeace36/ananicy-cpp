//
// Created by aviallon on 19/04/2021.
//

#ifndef ANANICY_CPP_RULES_HPP
#define ANANICY_CPP_RULES_HPP

#include "config.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

using nlohmann::json;

using ruleset = std::unordered_map<std::string, json>;

class Rules {
private:
  Config *config;
  ruleset program_rules{};
  ruleset type_rules{};
  ruleset cgroup_rules{};

public:
  explicit Rules(const std::filesystem::path &ruleset_directory,
                 Config                      *config_src);
  void load_rule_from_string(const std::string_view &rule);
  void load_rules_from_file(const std::filesystem::path &path);
  void load_rules_from_directory(const std::filesystem::path &dir_path);
  json get_rule(const std::string &name) const noexcept;

  void create_cgroups() const noexcept;

  size_t size() const noexcept;

  void show_all_rules();

  enum rule_type { rules, types, cgroups };
  void show_rules(rule_type type);
};

#endif // ANANICY_CPP_RULES_HPP
