#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <vector>

namespace process_info {
using autogroup = nlohmann::json;

using process_info = nlohmann::json;

nlohmann::json get_autogroup_map();
nlohmann::json get_process_info_map();
} // namespace process_info