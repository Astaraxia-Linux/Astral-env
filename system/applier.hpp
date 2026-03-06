#pragma once

#include "system_conf.hpp"
#include "differ.hpp"
#include <vector>

namespace astral_sys {

// Apply the diffs to the system
// Returns number of changes applied
int apply_diff(
    const std::vector<DiffEntry>& diffs,
    bool dry_run = false,
    bool prune = false,
    bool yes = false
);

// Rollback to a previous state
bool rollback(const std::string& snapshot_id = "");

// Check if rollback is fully implemented
bool rollback_implemented();

// List available rollback snapshots
std::vector<std::string> list_snapshots();

} // namespace astral_sys
