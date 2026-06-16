#include "system/system_conf.hpp"
#include "util/file.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace astral_sys {

namespace {

std::string trim(const std::string& s) {
    auto start = std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); });
    auto end = std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

std::string extract_value(const std::string& line) {
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) return "";
    
    std::string value = trim(line.substr(eq_pos + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

} // anonymous namespace

SystemConfig parse_system_config(const std::filesystem::path& path) {
    SystemConfig config;
    
    if (!std::filesystem::exists(path)) {
        return config;
    }
    
    std::string content = util::read_file(path);
    std::istringstream iss(content);
    
    bool in_system = false;
    bool in_packages = false;
    bool in_services = false;
    
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find("$ENV.System") != std::string::npos) {
            in_system = true; in_packages = false; in_services = false;
            continue;
        }
        if (line.find("$ENV.Packages") != std::string::npos) {
            in_system = false; in_packages = true; in_services = false;
            continue;
        }
        if (line.find("$ENV.Services") != std::string::npos) {
            in_system = false; in_packages = false; in_services = true;
            continue;
        }
        if (line == "};") {
            in_system = false; in_packages = false; in_services = false;
            continue;
        }
        
        // Check if it's a key=value line or a bare package name
        auto eq_pos = line.find('=');
        
        if (eq_pos != std::string::npos) {
            // This is a key = value line
            std::string key = trim(line.substr(0, eq_pos));
            std::string value = extract_value(line);
            
            if (in_system) {
                if (key == "hostname") config.hostname = value;
                else if (key == "timezone") config.timezone = value;
                else if (key == "locale") config.locale = value;
            } else if (in_packages) {
                config.packages.push_back(value.empty() ? key : value);
            } else if (in_services) {
                config.services.emplace_back(key, value);
            }
        } else if (in_packages && !line.empty() && line != "};") {
            // Bare package name (no = sign)
            config.packages.push_back(line);
        }
    }
    
    return config;
}

UserConfig parse_user_config(const std::filesystem::path& path) {
    UserConfig config;
    
    // Extract username from filename
    config.username = path.stem().string();
    
    if (!std::filesystem::exists(path)) {
        return config;
    }
    
    std::string content = util::read_file(path);
    std::istringstream iss(content);
    
    bool in_user = false;
    bool in_packages = false;
    bool in_dotfiles = false;
    bool in_vars = false;
    
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find("$ENV.User") != std::string::npos) {
            in_user = true; in_packages = false; in_dotfiles = false; in_vars = false;
            continue;
        }
        if (line.find("$ENV.Packages") != std::string::npos) {
            in_user = false; in_packages = true; in_dotfiles = false; in_vars = false;
            continue;
        }
        if (line.find("$ENV.Dotfiles") != std::string::npos) {
            in_user = false; in_packages = false; in_dotfiles = true; in_vars = false;
            continue;
        }
        if (line.find("$ENV.Vars") != std::string::npos) {
            in_user = false; in_packages = false; in_dotfiles = false; in_vars = true;
            continue;
        }
        if (line == "};") {
            in_user = false; in_packages = false; in_dotfiles = false; in_vars = false;
            continue;
        }
        
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = extract_value(line);
        
        if (in_user) {
            if (key == "name") config.username = value;
            else if (key == "shell") config.shell = value;
            else if (key == "groups") {
                std::istringstream gss(value);
                std::string g;
                while (gss >> g) config.groups.push_back(g);
            }
        } else if (in_packages) {
            config.packages.push_back(value.empty() ? key : value);
        } else if (in_dotfiles) {
            config.dotfiles.emplace_back(key, value);
        } else if (in_vars) {
            config.vars.emplace_back(key, value);
        }
    }
    
    return config;
}

std::vector<std::pair<std::string, UserConfig>> load_all_user_configs(
    const std::filesystem::path& config_dir
) {
    std::vector<std::pair<std::string, UserConfig>> configs;
    
    if (!std::filesystem::exists(config_dir)) {
        return configs;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(config_dir)) {
        if (!entry.is_regular_file()) continue;
        
        auto path = entry.path();
        if (path.extension() != ".stars") continue;
        
        // Skip env.stars
        if (path.stem() == "env") continue;
        
        auto user_config = parse_user_config(path);
        if (!user_config.username.empty()) {
            configs.emplace_back(user_config.username, user_config);
        }
    }
    
    return configs;
}

} // namespace astral_sys
