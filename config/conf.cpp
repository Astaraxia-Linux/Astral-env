#include "config/conf.hpp"
#include "util/file.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace config {

namespace {

std::string trim(const std::string& s) {
    auto start = std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); });
    auto end = std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : "";
}

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::optional<std::string> extract_value(const std::string& line) {
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
        return std::nullopt;
    }
    
    std::string value = trim(line.substr(eq_pos + 1));
    
    // Handle quoted strings
    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') ||
            (value.front() == '\'' && value.back() == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    
    return value;
}

} // anonymous namespace

AstralEnvConfig load(const std::filesystem::path& conf_path) {
    AstralEnvConfig config;
    
    if (!std::filesystem::exists(conf_path)) {
        // Return defaults if config doesn't exist
        return config;
    }
    
    std::string content = util::read_file(conf_path);
    std::istringstream iss(content);
    
    bool in_core_block = false;
    bool in_env_block = false;
    
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Track block state
        if (line.find("$AST.core") != std::string::npos) {
            in_core_block = true;
            in_env_block = false;
            continue;
        }
        if (line.find("$AST.env") != std::string::npos) {
            in_core_block = false;
            in_env_block = true;
            continue;
        }
        if (line == "};") {
            in_core_block = false;
            in_env_block = false;
            continue;
        }
        
        // Parse key = value
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = trim(line.substr(0, eq_pos));
        auto value_opt = extract_value(line);
        if (!value_opt) {
            continue;
        }
        std::string value = *value_opt;
        
        if (in_core_block) {
            if (key == "astral-env") {
                config.env_enabled = (to_lower(value) == "enabled");
            } else if (key == "astral-env-system") {
                config.system_enabled = (to_lower(value) == "enabled");
            }
        } else if (in_env_block) {
            if (key == "store_dir") {
                config.store_dir = value;
            } else if (key == "max_jobs") {
                try {
                    config.max_jobs = std::stoi(value);
                } catch (...) {
                    // Keep default
                }
            } else if (key == "gc_keep") {
                try {
                    config.gc_keep_days = std::stoi(value);
                } catch (...) {
                    // Keep default
                }
            }
        }
    }
    
    // Ensure default store directory exists
    if (config.store_dir.empty()) {
        config.store_dir = "/astral-env/store";
    }
    std::filesystem::create_directories(config.store_dir);
    
    return config;
}

std::string version() {
    return "1.1.0.0";
}

} // namespace config
