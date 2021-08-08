//
// Created by aviallon on 19/04/2021.
//

#include "core/rules.hpp"
#include "core/cgroups.hpp"
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stack>

using std::filesystem::path;

Rules::Rules(const std::filesystem::path &ruleset_directory, Config *config)
    : config(config) {
  if (!exists(ruleset_directory)) {
    spdlog::error("{}: directory {} does not exist", __func__,
                  ruleset_directory.string());
    return;
  }
  load_rules_from_directory(ruleset_directory);
}

void Rules::load_rule_from_string(const std::string &line) {
  json rule;

  if (line.empty() || line[0] == '#')
    return;

  try {
    rule = json::parse(line);
  } catch (const std::exception &e) {
    spdlog::warn("Error parsing JSON: {}, line: {}", e.what(), line);
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

void Rules::show_all_rules() {
  for (const auto &rule : cgroup_rules) {
    spdlog::debug("Cgroup: {}, {}", rule.first, rule.second.dump());
  }
  for (const auto &rule : type_rules) {
    spdlog::debug("Type: {}, {}", rule.first, rule.second.dump());
  }
  for (const auto &rule : program_rules) {
    spdlog::debug("Rule: {}, {}", rule.first, rule.second.dump());
  }
}

void Rules::show_rules(rule_type type) {
  const auto &rules = (type == rule_type::types)     ? type_rules
                      : (type == rule_type::cgroups) ? cgroup_rules
                                                     : program_rules;

  json rules_display;

  for (const auto &rule : rules) {
    if (type == rule_type::rules) {
      rules_display[rule.first] = get_rule(rule.first);
    } else {
      rules_display[rule.first] = rule.second;
    }
  }

  std::cout << rules_display.dump(4) << std::endl;
}

void Rules::load_rules_from_directory(const std::filesystem::path &dir_path) {
  std::stack<path> directories;
  directories.emplace(dir_path);

  while (!directories.empty()) {
    const path directory = directories.top();
    spdlog::debug("Scanning directory {}", directory.string());
    directories.pop();

    for (const path &file : std::filesystem::directory_iterator(directory)) {
      std::stringstream ss;
      ss << "Found file " << file.filename();

      if (std::filesystem::is_directory(file)) {
        ss << " (directory)";

        directories.emplace(file);
      }

      bool valid_file = false;

      if (config->cgroup_load() && file.extension().compare(".cgroups") == 0) {
        ss << " (CGROUP)";
        valid_file = true;
      } else if (config->type_load() &&
                 file.extension().compare(".types") == 0) {
        ss << " (TYPES)";
        valid_file = true;
      } else if (config->rule_load() &&
                 file.extension().compare(".rules") == 0) {
        ss << " (RULES)";
        valid_file = true;
      }

      if (valid_file) {
        load_rules_from_file(file);
      }

      spdlog::trace(ss.str());
    }
  }
}

void Rules::create_cgroups() {
  for (const auto &cgroup_rule : cgroup_rules) {
    control_groups::create_cgroup(cgroup_rule.first);
    if (cgroup_rule.second.contains("CPUQuota")) {
      control_groups::set_cgroup_cpu_quota(
          cgroup_rule.first, cgroup_rule.second["CPUQuota"].get<unsigned>());
    }
  }
}

json Rules::get_rule(const std::string &name) {
  json rule;
  if (program_rules.contains(name)) {
    rule = program_rules.at(name);
    if (rule.contains("type") && type_rules.contains(rule["type"])) {
      json type_rule =
          type_rules.at(rule["type"]); // We create a copy of the type rule to
                                       // avoid overwriting it
      type_rule.merge_patch(
          rule); // We then merge the rule into it, overriding parameters from
                 // the type with those explicitly specified in the rule
      rule.merge_patch(type_rule); // We then merge it back into the rule
    }
  }
  return rule;
}

size_t Rules::size() const { return program_rules.size(); }
