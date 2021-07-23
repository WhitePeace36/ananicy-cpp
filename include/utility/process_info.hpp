#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <vector>

namespace process_info {
using autogroup = nlohmann::json;

using process_info = nlohmann::json;

std::map<unsigned, autogroup> get_autogroup_map();
nlohmann::json                get_process_info_map();
} // namespace process_info