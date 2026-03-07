#include "env/shell.hpp"
#include "env/activation.hpp"
#include "lock/lockfile.hpp"
#include "util/file.hpp"
#include "util/process.hpp"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>

namespace env {

namespace {

EnvConfig load_env_config(const std::filesystem::path& lock_path, const std::filesystem::path& stars_path) {
    EnvConfig config;
    
    // Load lockfile
    auto lock = lock::read(lock_path);
    
    for (const auto& entry : lock.entries) {
        // Skip system entries - they don't have store paths
        if (entry.build_kind != lock::BuildKind::System && !entry.store_path.empty()) {
            config.store_paths.push_back(entry.store_path);
        }
    }
    
    // Load .stars file for additional config
    if (std::filesystem::exists(stars_path)) {
        std::string content = util::read_file(stars_path);
        std::istringstream iss(content);
        
        bool in_metadata = false;
        bool in_vars = false;
        bool in_shell = false;
        
        std::string line;
        while (std::getline(iss, line)) {
            // Simple parsing - trim and check
            auto trim = [](std::string& s) {
                auto start = s.find_first_not_of(" \t");
                auto end = s.find_last_not_of(" \t");
                if (start == std::string::npos) { s = ""; return; }
                s = s.substr(start, end - start + 1);
            };
            trim(line);
            
            if (line.empty() || line[0] == '#') continue;
            
            if (line.find("$ENV.Metadata") != std::string::npos) {
                in_metadata = true; in_vars = false; in_shell = false;
                continue;
            }
            if (line.find("$ENV.Vars") != std::string::npos) {
                in_metadata = false; in_vars = true; in_shell = false;
                continue;
            }
            if (line.find("$ENV.Shell") != std::string::npos) {
                in_metadata = false; in_vars = false; in_shell = true;
                continue;
            }
            if (line == "};") {
                in_metadata = false; in_vars = false; in_shell = false;
                continue;
            }
            
            auto eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                trim(key); trim(value);
                
                // Remove quotes
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                
                if (in_metadata && key == "Name") {
                    config.name = value;
                } else if (in_vars) {
                    config.vars.emplace_back(key, value);
                }
            } else if (in_shell && !line.empty()) {
                config.shell_hook += line + "\n";
            }
        }
    }
    
    if (config.name.empty()) {
        config.name = "unknown";
    }
    
    return config;
}

} // anonymous namespace

void enter_shell(
    const std::filesystem::path& lock_path,
    const std::filesystem::path& stars_path
) {
    auto config = load_env_config(lock_path, stars_path);
    
    // Generate and write activation script
    auto script = generate_activation_script(config);
    auto script_path = get_activation_script_path();
    
    util::write_file(script_path, script);
    
    // Execute shell with activation script sourced
    std::cout << "Entering environment: " << config.name << "\n";
    
    // Get the user's shell from $SHELL - FIX: actually use it instead of hardcoding /bin/bash
    const char* shell_env = std::getenv("SHELL");
    if (!shell_env) shell_env = "/bin/bash";
    std::filesystem::path shell_path(shell_env);
    
    // Determine how to source the activation script based on shell type
    std::string shell_name = shell_path.filename().string();
    std::vector<std::string> exec_args;
    std::vector<std::pair<std::string, std::string>> env_additions;

    if (shell_name == "bash") {
        // Use --rcfile for interactive shells
        exec_args = {"--rcfile", script_path.string()};
    } else if (shell_name == "zsh") {
        exec_args = {"--rcs", script_path.string()};
    } else {
        // For other shells (sh, dash), use ENV variable (POSIX)
        env_additions = {{"ENV", script_path.string()}};
    }

    // Execute the user's shell with the activation script
    util::exec_replace(shell_path, exec_args, env_additions);
}

int run_command(
    const std::filesystem::path& lock_path,
    const std::filesystem::path& stars_path,
    const std::vector<std::string>& command
) {
    auto config = load_env_config(lock_path, stars_path);
    
    // Generate and write activation script
    auto script = generate_activation_script(config);
    auto script_path = get_activation_script_path();
    
    util::write_file(script_path, script);
    
    // Build the command
    std::string cmd = "source " + script_path.string() + " && ";
    for (size_t i = 0; i < command.size(); ++i) {
        if (i > 0) cmd += " ";
        cmd += command[i];
    }
    
    // FIX: Use $SHELL instead of hardcoded /bin/bash
    const char* shell_env = std::getenv("SHELL");
    if (!shell_env) shell_env = "/bin/sh";
    
    auto result = util::run(shell_env, {"-c", cmd});
    
    std::cout << result.stdout_output;
    std::cerr << result.stderr_output;
    
    return result.exit_code;
}

} // namespace env
