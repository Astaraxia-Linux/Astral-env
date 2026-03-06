#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace repo {

struct BinEntry {
    std::string name;
    std::string version;
    std::string category;
    std::string description;
    std::string homepage;
    std::string url;
    std::string checksum;
    
    std::vector<std::string> provides_bin;
    std::vector<std::string> provides_lib;
    std::vector<std::string> provides_inc;
    std::vector<std::string> provides_pkg;
    
    std::optional<std::string> build_script; // nullopt = pre-built tarball
    bool via_astral = false;  // if true, install via "astral -S" instead of to store
};

// Parse a single /bin .stars file
BinEntry parse_entry(const std::filesystem::path& stars_file);

// Find all entries for a package name across /bin/
// Returns multiple versions if they exist (python.stars, python-3.11.stars, etc.)
std::vector<BinEntry> find_entries(
    const std::filesystem::path& repo_bin_dir,
    std::string_view package_name
);

// Find the best entry for a package name (latest version)
std::optional<BinEntry> find_latest(
    const std::filesystem::path& repo_bin_dir,
    std::string_view package_name
);

// List all categories in the repo
std::vector<std::string> list_categories(const std::filesystem::path& repo_bin_dir);

// List all packages in a category
std::vector<std::string> list_packages(const std::filesystem::path& repo_bin_dir, const std::string& category);

} // namespace repo
