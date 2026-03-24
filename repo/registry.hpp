#pragma once

#include "env/node.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace repo {

// Repo layout on the remote:
//   <base>/astral.index     — package index
//   <base>/recipes/<cat>/<s>/<pkg>/<pkg>-<ver>-<arch>.stars
//   <base>/bin/<cat>/<pkg>-<ver>-<arch>.tar.*
//   <base>/font/<family>/<font>.tar.*

struct RepoEntry {
    std::string name;
    std::string raw_url;    // resolved https:// base URL (no trailing slash)
    std::string index_url;  // raw_url + /astral.index
    bool        enabled = true;
};

// Expand shorthand URLs from env.stars:
//   "github::Owner/Repo"  →  https://raw.githubusercontent.com/Owner/Repo/refs/heads/main
//   "codeberg::Owner/Repo" → https://codeberg.org/Owner/Repo/raw/branch/main
//   bare https:// URL      → used as-is
std::string expand_url(const std::string& url);

// Parse $ENV.System.Packages.Repository block into a list of repos.
// Falls back to AOHARU if the block is missing or empty.
std::vector<RepoEntry> parse_repos(const env::NodeMap& root);

// Paths under a repo base URL
std::string index_url(const RepoEntry& r);
std::string recipe_url(const RepoEntry& r, const std::string& cat,
                        const std::string& shard, const std::string& pkg,
                        const std::string& filename);
std::string bin_url(const RepoEntry& r, const std::string& cat,
                    const std::string& filename);
std::string font_url(const RepoEntry& r, const std::string& family,
                     const std::string& filename);

} // namespace repo
