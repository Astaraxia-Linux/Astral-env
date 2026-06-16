#pragma once

#include "system_conf.hpp"
#include <vector>
#include <string>

namespace astral_sys {

enum class DiffAction {
    Install,      // Package needs to be installed
    Remove,       // Package needs to be removed (with --prune)
    Enable,       // Service needs to be enabled
    Disable,      // Service needs to be disabled
    Change,       // Value needs to be changed
    Symlink,      // Dotfile needs to be linked
    Unlink,       // Dotfile link needs to be removed
    Conflict,     // Existing file blocks the change
};

struct DiffEntry {
    std::string type;     // "package", "service", "system", "dotfile", "var"
    std::string name;
    DiffAction action;
    std::string current;  // Current state (if known)
    std::string target;   // Target state
};

// Compute diff between declared config and current system state
std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    bool global_only = false,
    const std::string& target_user = ""
);

// Compute diff with user configs
std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    const std::vector<std::pair<std::string, UserConfig>>& user_configs
);

// Print diff entries to stdout
void print_diff(const std::vector<DiffEntry>& diffs);

} // namespace astral_sys
