#include "util/process.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <array>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace util {

ProcessResult run(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::optional<std::filesystem::path>& working_dir,
    const std::vector<std::pair<std::string, std::string>>& extra_env
) {
    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdout_pipe) == -1) {
        throw std::runtime_error("Failed to create stdout pipe");
    }
    if (pipe(stderr_pipe) == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        throw std::runtime_error("Failed to create stderr pipe");
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        throw std::runtime_error("Failed to fork process");
    }
    
    if (pid == 0) {
        // Child process
        
        // Close read ends
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        // Redirect stdout and stderr
        if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }
        if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
            _exit(127);
        }
        
        // Close original write ends
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Change working directory if specified
        if (working_dir.has_value()) {
            if (chdir(working_dir->c_str()) == -1) {
                _exit(127);
            }
        }
        
        // Set extra environment variables
        for (const auto& [key, value] : extra_env) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        
        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(executable.c_str(), argv.data());
        _exit(127);
    }
    
    // Parent process
    
    // Close write ends
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    // Read output
    std::string stdout_output;
    std::string stderr_output;
    
    std::array<char, 4096> buffer;
    ssize_t n;
    
    while ((n = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
        stdout_output.append(buffer.data(), n);
    }
    close(stdout_pipe[0]);
    
    while ((n = read(stderr_pipe[0], buffer.data(), buffer.size())) > 0) {
        stderr_output.append(buffer.data(), n);
    }
    close(stderr_pipe[0]);
    
    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    
    ProcessResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_output = std::move(stdout_output);
    result.stderr_output = std::move(stderr_output);
    
    return result;
}

[[noreturn]] void exec_replace(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::vector<std::pair<std::string, std::string>>& env_additions
) {
    // Set extra environment variables
    for (const auto& [key, value] : env_additions) {
        setenv(key.c_str(), value.c_str(), 1);
    }
    
    // Build argv
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    
    execvp(executable.c_str(), argv.data());
    
    // If we get here, exec failed
    std::fprintf(stderr, "Failed to execute %s: %s\n", 
                 executable.c_str(), std::strerror(errno));
    std::exit(127);
}

} // namespace util
