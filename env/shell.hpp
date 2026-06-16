#pragma once

#include <filesystem>
#include <vector>
#include <string>

namespace env {

// Enter an interactive shell with the environment active
void enter_shell(
    const std::filesystem::path& lock_path,
    const std::filesystem::path& stars_path
);

// Run a single command inside the environment
int run_command(
    const std::filesystem::path& lock_path,
    const std::filesystem::path& stars_path,
    const std::vector<std::string>& command
);

} // namespace env
