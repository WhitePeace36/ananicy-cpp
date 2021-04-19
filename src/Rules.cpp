//
// Created by aviallon on 19/04/2021.
//

#include "Rules.hpp"
#include <iostream>
#include <fstream>

using std::filesystem::path;

Rules::Rules(const std::filesystem::path& ruleset_directory) {
    load_rules_from_directory(ruleset_directory);
}

void Rules::load_rule_from_string(const std::string& line) {
    json rule;

    if (line.empty() || line[0] == '#')
        return;

    try {
        rule = json::parse(line);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        return;
    }


    if (rule.contains("name") && rule["name"].is_string()) { /* Program rule */
        program_rules.insert_or_assign(rule["name"], rule);
    } else if (rule.contains("type") && rule["type"].is_string()) { /* Type rule */
        type_rules.insert_or_assign(rule["type"], rule);
    } else if (rule.contains("cgroup") && rule["cgroup"].is_string()) { /* Cgroup rule */
        cgroup_rules.insert_or_assign(rule["cgroup"], rule);
    } else {
        std::cerr << "Rule does not have a name, or name is not a string! "  << rule.dump() << std::endl;
        // throw std::runtime_error("Rule does not have a name, or name is not a string!");
    }
}

void Rules::load_rules_from_file(const std::filesystem::path& path) {
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
        std::cout << "Cgroup: " << rule.first << ", " << rule.second.dump() << std::endl;
    }
    for (const std::pair<std::string, json> rule : type_rules) {
        std::cout << "Type: " << rule.first << ", " << rule.second.dump() << std::endl;
    }
    for (const std::pair<std::string, json> rule : program_rules) {
        std::cout << "Rule: " << rule.first << ", " << rule.second.dump() << std::endl;
    }
}

void Rules::load_rules_from_directory(const std::filesystem::path& dir_path) {
    for (const path& file : std::filesystem::directory_iterator(dir_path)) {
        std::stringstream ss;
        ss << "Found file " << file.filename();

        if (std::filesystem::is_directory(file)) {
            ss << " (directory)";

            load_rules_from_directory(file);
        }

        bool valid_file = false;

        if (file.extension().compare(".cgroups") == 0) {
            ss << " CGROUP!!!";
            valid_file = true;
        } else if (file.extension().compare(".types") == 0) {
            ss << " TYPES!!!";
            valid_file = true;
        } else if (file.extension().compare(".rules") == 0) {
            ss << " RULES!!!";
            valid_file = true;
        }


        if (valid_file) {
            load_rules_from_file(file);
        }

        ss << std::endl;
        std::cout << ss.str() << std::flush;
    }
}
