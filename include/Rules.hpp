//
// Created by aviallon on 19/04/2021.
//

#ifndef ANANICY_CPP_RULES_HPP
#define ANANICY_CPP_RULES_HPP

#include <map>
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

using nlohmann::json;

using ruleset = std::unordered_map<std::string, json>;

class Rules {
private:
    ruleset program_rules;
    ruleset type_rules;
    ruleset cgroup_rules;

public:
    explicit Rules(const std::filesystem::path& ruleset_directory);
    void load_rule_from_string(const std::string& rule);
    void load_rules_from_file(const std::filesystem::path& path);
    void load_rules_from_directory(const std::filesystem::path& dir_path);
    json get_rule(const std::string& name);

    void show_rules();
};


#endif //ANANICY_CPP_RULES_HPP
