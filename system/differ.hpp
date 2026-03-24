#pragma once

#include "system/system_conf.hpp"
#include <string>
#include <vector>

namespace astral_sys {

enum class DiffAction {
    Install,
    Remove,
    Enable,
    Disable,
    Change,
    Symlink,
    Unlink,
    Conflict,
};

struct DiffEntry {
    std::string type;
    std::string name;
    DiffAction  action;
    std::string current;
    std::string target;
};

std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    const std::vector<std::pair<std::string, UserConfig>>& user_configs = {}
);

void print_diff(const std::vector<DiffEntry>& diffs);

} // namespace astral_sys
