#pragma once

#include "system/system_conf.hpp"
#include "system/differ.hpp"
#include "repo/registry.hpp"
#include <string>
#include <vector>

namespace astral_sys {

// Apply diff entries (packages, services, symlinks, hostname, timezone)
int apply_diff(const std::vector<DiffEntry>& diffs,
               bool dry_run = false,
               bool prune   = false,
               bool yes     = false);

// Full system apply — runs apply_diff plus all hardware/system applicators
// that aren't captured by the diff (locale, console, kernel modules, etc.)
int apply_full(const SystemConfig& cfg,
               const std::vector<std::pair<std::string, UserConfig>>& user_configs,
               const std::vector<repo::RepoEntry>& repos,
               bool dry_run = false,
               bool yes     = false);

bool rollback(const std::string& snapshot_id = "");
bool rollback_implemented();
std::vector<std::string> list_snapshots();

} // namespace astral_sys
