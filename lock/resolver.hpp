#pragma once

#include <filesystem>
#include <string>
#include <optional>

// Forward declaration
namespace repo { struct BinEntry; }

namespace lock {

struct VersionConstraint {
    std::string package;
    std::string op;      // "=", ">=", "<=", ">", "<", "" (any)
    std::string version; // empty if op is ""
};

// Parse a constraint string like "python >= 3.11"
VersionConstraint parse_constraint(std::string_view raw);

// Find the best matching entry satisfying the constraint
// Returns nullopt if nothing satisfies it
std::optional<repo::BinEntry> resolve(
    const std::filesystem::path& repo_bin_dir,
    const VersionConstraint& constraint
);

} // namespace lock
