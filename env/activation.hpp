#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace env {

struct EnvConfig {
    std::string name;
    std::string description;
    std::vector<std::filesystem::path> store_paths;
    std::vector<std::string> provides_bin;
    std::vector<std::string> provides_lib;
    std::vector<std::string> provides_inc;
    std::vector<std::string> provides_pkg;
    std::vector<std::pair<std::string, std::string>> vars;
    std::string shell_hook;
};

// Generate activation script from lockfile entries
std::string generate_activation_script(const EnvConfig& config);

// Get activation script path
std::filesystem::path get_activation_script_path();

} // namespace env
