//
// Created by aviallon on 19/04/2021.
//

#include "rules.hpp"
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

using std::filesystem::path;

Rules::Rules(const std::filesystem::path &ruleset_directory, Config *config)
    : config(config) {
  load_rules_from_directory(ruleset_directory);
}

void Rules::load_rule_from_string(const std::string &line) {
  json rule;

  if (line.empty() || line[0] == '#')
    return;

  try {
    rule = json::parse(line);
  } catch (const std::exception &e) {
    spdlog::warn("Error parsing JSON: {}", e.what());
    return;
  }

  if (rule.contains("name") && rule["name"].is_string()) { /* Program rule */
    program_rules.insert_or_assign(rule["name"], rule);
  } else if (rule.contains("type") &&
             rule["type"].is_string()) { /* Type rule */
    type_rules.insert_or_assign(rule["type"], rule);
  } else if (rule.contains("cgroup") &&
             rule["cgroup"].is_string()) { /* Cgroup rule */
    cgroup_rules.insert_or_assign(rule["cgroup"], rule);
  } else {
    spdlog::error("Rule does not have a name, or name is not a string: {}",
                  rule.dump());
  }
}

void Rules::load_rules_from_file(const std::filesystem::path &path) {
  std::ifstream rule_file;
  rule_file.open(path);
  std::string line;

  while (rule_file.good()) {
    std::getline(rule_file, line);

    load_rule_from_string(line);
  }
}

void Rules::show_rules() {
  for (const std::pair<std::string, json> rule : cgroup_rules) {
    spdlog::debug("Cgroup: {}, {}", rule.first, rule.second.dump());
    // std::cout << "Cgroup: " << rule.first << ", " << rule.second.dump()
    // << std::endl;
  }
  for (const std::pair<std::string, json> rule : type_rules) {
    spdlog::debug("Type: {}, {}", rule.first, rule.second.dump());
  }
  for (const std::pair<std::string, json> rule : program_rules) {
    spdlog::debug("Rule: {}, {}", rule.first, rule.second.dump());
  }
}

void Rules::load_rules_from_directory(
    const std::filesystem::path &dir_path) { // NOLINT(misc-no-recursion)
  for (const path &file : std::filesystem::directory_iterator(dir_path)) {
    std::stringstream ss;
    ss << "Found file " << file.filename();

    if (std::filesystem::is_directory(file)) {
      ss << " (directory)";

      load_rules_from_directory(file);
    }

    bool valid_file = false;

    if (config->cgroup_load() && file.extension().compare(".cgroups") == 0) {
      ss << " (CGROUP)";
      valid_file = true;
    } else if (config->type_load() && file.extension().compare(".types") == 0) {
      ss << " (TYPES)";
      valid_file = true;
    } else if (config->rule_load() && file.extension().compare(".rules") == 0) {
      ss << " (RULES)";
      valid_file = true;
    }

    if (valid_file) {
      load_rules_from_file(file);
    }

    spdlog::trace(ss.str());
  }
}

json Rules::get_rule(const std::string &name) {
  json rule;
  if (program_rules.contains(name)) {
    rule = program_rules.at(name);
    if (rule.contains("type") && type_rules.contains(rule["type"])) {
        json type_rule = type_rules.at(rule["type"]); // We create a copy of the type rule to avoid overwriting it
        type_rule.merge_patch(rule); // We then merge the rule into it, overriding parameters from the type with those explicitly specified in the rule
        rule.merge_patch(type_rule); // We then merge it back into the rule
    }
  }
  return rule;
}

size_t Rules::size() const { return program_rules.size(); }
