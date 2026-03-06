#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace util {

struct ProcessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

// Run a process, capture output, return result
// Throws on exec failure — not on non-zero exit code
ProcessResult run(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::optional<std::filesystem::path>& working_dir = std::nullopt,
    const std::vector<std::pair<std::string, std::string>>& extra_env = {}
);

// Replace current process (exec) — used for astral-env shell
// Does not return on success
[[noreturn]] void exec_replace(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::vector<std::pair<std::string, std::string>>& env_additions = {}
);

} // namespace util
