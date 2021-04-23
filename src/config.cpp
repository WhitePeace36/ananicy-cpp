//
// Created by aviallon on 20/04/2021.
//

#include "config.hpp"
#include <fstream>
#include <spdlog/spdlog.h>

Config::Config(const std::filesystem::path &config_path) {
  config = std::unordered_map<std::string, std::string>();
  std::ifstream is_file;
  is_file.open(config_path);

  std::string line;
  while (std::getline(is_file, line)) {
    std::istringstream is_line(line);
    std::string        key;
    if (std::getline(is_line, key, '=')) {
      std::string value;
      if (std::getline(is_line, value))
        config[key] = value;
    }
  }
}

void Config::show() {
  for (const auto &item : config) {
    spdlog::info("Config {}: {}", item.first, item.second);
  }
}

void Config::set(const std::string &key, const std::string &value) {
  config[key] = value;
}

std::optional<std::string> Config::get(const std::string &key) const {
  if (config.contains(key))
    return config.at(key);

  return {};
}
