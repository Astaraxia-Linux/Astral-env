#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace astral_sys {

struct SystemConfig {
    std::string hostname;
    std::string timezone;
    std::string locale;
    std::vector<std::string> packages;
    std::vector<std::pair<std::string, std::string>> services; // name -> state
};

struct UserConfig {
    std::string username;
    std::string shell;
    std::vector<std::string> groups;
    std::vector<std::string> packages;
    std::vector<std::pair<std::string, std::string>> dotfiles; // dest -> source
    std::vector<std::pair<std::string, std::string>> vars;
};

// Parse system config from env.stars
SystemConfig parse_system_config(const std::filesystem::path& path);

// Parse user config from <username>.stars
UserConfig parse_user_config(const std::filesystem::path& path);

// Get all user config files in a directory
std::vector<std::pair<std::string, UserConfig>> load_all_user_configs(
    const std::filesystem::path& config_dir
);

} // namespace astral_sys
