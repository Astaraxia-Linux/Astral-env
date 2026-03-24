#pragma once

#include "repo/registry.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace repo {

struct PkgEntry {
    std::string cat_pkg;  // "app-misc/neofetch"
    std::string arch;
    std::string version;
    std::string repo_name;
};

// Download and cache index files for all repos.
// Cached under /var/lib/astral-env/index/<repo-name>.
void sync_indexes(const std::vector<RepoEntry>& repos,
                  const std::filesystem::path& cache_dir = "/var/lib/astral-env/index");

// Query cached indexes for a package name.
std::vector<PkgEntry> query(const std::string& pkg_name,
                             const std::vector<RepoEntry>& repos,
                             const std::filesystem::path& cache_dir = "/var/lib/astral-env/index");

// Parse a single index file into PkgEntry list.
std::vector<PkgEntry> parse_index(const std::filesystem::path& index_file,
                                   const std::string& repo_name);

} // namespace repo
