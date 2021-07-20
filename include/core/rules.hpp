//
// Created by aviallon on 19/04/2021.
//

#ifndef ANANICY_CPP_RULES_HPP
#define ANANICY_CPP_RULES_HPP

#include "config.hpp"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

using nlohmann::json;

using ruleset = std::unordered_map<std::string, json>;

class Rules {
private:
  ruleset program_rules;
  ruleset type_rules;
  ruleset cgroup_rules;
  Config *config;

public:
  explicit Rules(const std::filesystem::path &ruleset_directory,
                 Config *                     config);
  void load_rule_from_string(const std::string &rule);
  void load_rules_from_file(const std::filesystem::path &path);
  void load_rules_from_directory(const std::filesystem::path &dir_path);
  json get_rule(const std::string &name);

  void create_cgroups();

  size_t size() const;

  void show_rules();
};

#endif // ANANICY_CPP_RULES_HPP
