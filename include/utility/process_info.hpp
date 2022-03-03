#pragma once

#include <map>
#include <vector>

#include <nlohmann/json.hpp>

namespace process_info {
using autogroup = nlohmann::json;

using process_info = nlohmann::json;

bool is_realtime(pid_t pid);

nlohmann::json get_autogroup_map();
nlohmann::json get_process_info_map();
} // namespace process_info
