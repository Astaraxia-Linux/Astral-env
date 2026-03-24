#include "env/shell.hpp"
#include "env/activation.hpp"
#include "lock/lockfile.hpp"
#include "util/file.hpp"
#include "util/process.hpp"

#include <iostream>
#include <sstream>
#include <cstdlib>

namespace env {

namespace {

EnvConfig load_env_config(const std::filesystem::path& lock_path,
                           const std::filesystem::path& stars_path) {
    EnvConfig config;
    auto lock = lock::read(lock_path);
    for (const auto& entry : lock.entries) {
        if (entry.build_kind != lock::BuildKind::System && !entry.store_path.empty())
            config.store_paths.push_back(entry.store_path);
    }

    if (std::filesystem::exists(stars_path)) {
        NodeMap root = parse_file(stars_path);
        Node r(root);
        if (auto p = r.get_path("Metadata.Name"); p && (*p)->is_str()) config.name = (*p)->str();
        if (auto p = r.get("Vars"); p && (*p)->is_map()) {
            for (const auto& [k, v] : (*p)->map())
                if (v.is_str()) config.vars.emplace_back(k, v.str());
        }
        if (auto p = r.get("Shell"); p && (*p)->is_map()) {
            for (const auto& [k, v] : (*p)->map())
                if (v.is_str()) config.shell_hook += v.str() + "\n";
        }
    }
    if (config.name.empty()) config.name = "env";
    return config;
}

} // anonymous namespace

void enter_shell(const std::filesystem::path& lock_path,
                 const std::filesystem::path& stars_path) {
    auto config      = load_env_config(lock_path, stars_path);
    auto script      = generate_activation_script(config);
    auto script_path = get_activation_script_path();
    util::write_file(script_path, script);

    std::cout << "Entering environment: " << config.name << "\n";

    const char* shell_env = std::getenv("SHELL");
    if (!shell_env) shell_env = "/bin/sh";
    std::filesystem::path shell_path(shell_env);
    std::string shell_name = shell_path.filename().string();

    std::vector<std::string>                         args;
    std::vector<std::pair<std::string, std::string>> env_add;

    if (shell_name == "bash") {
        args = {"--rcfile", script_path.string()};
    } else if (shell_name == "zsh") {
        env_add = {{"ZDOTDIR", script_path.parent_path().string()}};
    } else {
        // POSIX: ENV variable is sourced by interactive sh/dash
        env_add = {{"ENV", script_path.string()}};
    }

    util::exec_replace(shell_path, args, env_add);
}

int run_command(const std::filesystem::path& lock_path,
                const std::filesystem::path& stars_path,
                const std::vector<std::string>& command) {
    auto config      = load_env_config(lock_path, stars_path);
    auto script      = generate_activation_script(config);
    auto script_path = get_activation_script_path();
    util::write_file(script_path, script);

    // Use . (dot) not source — POSIX sh compatible
    std::string cmd = ". " + script_path.string();
    for (const auto& arg : command) cmd += " && " + arg;

    const char* shell_env = std::getenv("SHELL");
    if (!shell_env) shell_env = "/bin/sh";

    auto result = util::run(shell_env, {"-c", cmd});
    std::cout << result.stdout_output;
    std::cerr << result.stderr_output;
    return result.exit_code;
}

} // namespace env
