#include "system/differ.hpp"
#include "system/system_conf.hpp"
#include "util/process.hpp"
#include "util/file.hpp"
#include <iostream>
#include <algorithm>
#include <set>

namespace astral_sys {

namespace {

// Check if a package is installed (using pacman directly since astral doesn't have -Q)
bool is_package_installed(const std::string& package) {
    auto result = util::run("pacman", {"-Q", package});
    return result.exit_code == 0;
}

// Check if a service is enabled
std::string get_service_state(const std::string& service) {
    auto result = util::run("systemctl", {"is-enabled", service});
    if (result.exit_code == 0) return "enabled";
    if (result.exit_code == 1) return "disabled";
    return "unknown";
}

// Check current hostname
std::string get_hostname() {
    auto result = util::run("hostname", {});
    // Trim newline
    if (!result.stdout_output.empty() && result.stdout_output.back() == '\n') {
        result.stdout_output.pop_back();
    }
    return result.stdout_output;
}

// Check current timezone
std::string get_timezone() {
    auto result = util::run("timedatectl", {"show", "-p", "Timezone", "--value"});
    if (!result.stdout_output.empty() && result.stdout_output.back() == '\n') {
        result.stdout_output.pop_back();
    }
    return result.stdout_output;
}

// Check if a file is a symlink
bool is_symlink(const std::filesystem::path& path) {
    return std::filesystem::is_symlink(path);
}

} // anonymous namespace

std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    bool,
    const std::string& target_user
) {
    std::vector<DiffEntry> diffs;
    
    // Check hostname
    std::string current_hostname = get_hostname();
    if (!declared.hostname.empty() && declared.hostname != current_hostname) {
        diffs.push_back({
            "system",
            "hostname",
            DiffAction::Change,
            current_hostname,
            declared.hostname
        });
    }
    
    // Check timezone
    std::string current_timezone = get_timezone();
    if (!declared.timezone.empty() && declared.timezone != current_timezone) {
        diffs.push_back({
            "system",
            "timezone",
            DiffAction::Change,
            current_timezone,
            declared.timezone
        });
    }
    
    // Check packages
    for (const auto& package : declared.packages) {
        if (!is_package_installed(package)) {
            diffs.push_back({
                "package",
                package,
                DiffAction::Install,
                "not installed",
                "will install"
            });
        }
    }
    
    // Check services
    for (const auto& [service, target_state] : declared.services) {
        std::string current_state = get_service_state(service);
        
        if (target_state == "enabled" && current_state != "enabled") {
            diffs.push_back({
                "service",
                service,
                DiffAction::Enable,
                current_state,
                "will enable"
            });
        } else if (target_state == "disabled" && current_state != "disabled") {
            diffs.push_back({
                "service",
                service,
                DiffAction::Disable,
                current_state,
                "will disable"
            });
        }
    }
    
    return diffs;
}

std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    const std::vector<std::pair<std::string, UserConfig>>& user_configs
) {
    // First get the base system diffs
    auto diffs = diff_system(declared, true, "");
    
    // Then add user-specific diffs
    for (const auto& [username, user_config] : user_configs) {
        // Check dotfiles
        for (const auto& [dest, source] : user_config.dotfiles) {
            std::string current_state = "not linked";
            if (std::filesystem::exists(dest)) {
                if (std::filesystem::is_symlink(dest)) {
                    current_state = "linked";
                } else {
                    current_state = "exists (not a symlink)";
                }
            }
            
            diffs.push_back({
                "dotfile",
                dest,
                DiffAction::Symlink,
                current_state,
                source
            });
        }
    }
    
    return diffs;
}

void print_diff(const std::vector<DiffEntry>& diffs) {
    if (diffs.empty()) {
        std::cout << "No changes needed.\n";
        return;
    }
    
    std::cout << "Changes:\n";
    for (const auto& d : diffs) {
        std::string action_str;
        switch (d.action) {
            case DiffAction::Install: action_str = "[+]"; break;
            case DiffAction::Remove: action_str = "[-]"; break;
            case DiffAction::Enable: action_str = "[~]"; break;
            case DiffAction::Disable: action_str = "[~]"; break;
            case DiffAction::Change: action_str = "[~]"; break;
            case DiffAction::Symlink: action_str = "[+]"; break;
            case DiffAction::Unlink: action_str = "[-]"; break;
            case DiffAction::Conflict: action_str = "[!]"; break;
        }
        
        std::cout << "  " << action_str << " " << d.type << ": " << d.name;
        if (!d.current.empty() || !d.target.empty()) {
            std::cout << " - " << d.current << " -> " << d.target;
        }
        std::cout << "\n";
    }
}

} // namespace astral_sys
