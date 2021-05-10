//
// Created by aviallon on 20/04/2021.
//

#ifndef ANANICY_CPP_CONFIG_HPP
#define ANANICY_CPP_CONFIG_HPP

#include <filesystem>
#include <optional>
#include <unordered_map>

class Config {
public:
  explicit Config(const std::filesystem::path &config_path);

  bool apply_nice() {
    if (config.contains("apply_nice"))
      return config["apply_nice"] == "true";

    return true;
  }

  bool apply_sched() {
    if (config.contains("apply_sched"))
      return config["apply_sched"] == "true";

    return true;
  }
  bool apply_ionice() {
    if (config.contains("apply_ionice"))
      return config["apply_ionice"] == "true";

    return true;
  }
  bool apply_oom_score_adj() {
    if (config.contains("apply_oom_score_adj"))
      return config["apply_oom_score_adj"] == "true";

    return true;
  }
  bool cgroup_load() {
    if (config.contains("cgroup_load"))
      return config["cgroup_load"] == "true";

    return true;
  }
  bool type_load() {
    if (config.contains("type_load"))
      return config["type_load"] == "true";

    return true;
  }
  bool rule_load() {
    if (config.contains("rule_load"))
      return config["rule_load"] == "true";

    return true;
  }

  void show();
  void set(const std::string &key, const std::string &value);
  std::optional<std::string> get(const std::string &key) const;

private:
  std::unordered_map<std::string, std::string> config;
};

#endif // ANANICY_CPP_CONFIG_HPP
