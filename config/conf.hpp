#pragma once

#include <filesystem>
#include <string>
#include <optional>

namespace config {

struct AstralEnvConfig {
    bool env_enabled = false;
    bool system_enabled = false;
    std::filesystem::path store_dir = "/astral-env/store";
    int max_jobs = 4;
    int gc_keep_days = 30;
};

// Parse /etc/astral/astral.stars and extract $AST.core + $AST.env blocks
// Throws std::runtime_error on parse failure
AstralEnvConfig load(
    const std::filesystem::path& conf_path = "/etc/astral/astral.stars"
);

// Get version string
std::string version();

} // namespace config
