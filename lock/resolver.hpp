#pragma once

#include <filesystem>
#include <string>
#include <optional>

// Forward declaration
namespace repo { struct BinEntry; }

namespace lock {

struct VersionConstraint {
    std::string package; // raw name as written (may include -bin suffix)
    std::string op;      // "=", ">=", "<=", ">", "<", "" (any version)
    std::string version; // empty when op is ""
};

enum class ResolveKind {
    BinRepo,      // Found in AOHARU bin/ (prebuilt or source recipe with build_script)
    AstralSource, // Not in bin/ — will be built from source via astral recipe system
};

struct ResolveResult {
    ResolveKind kind;
    std::optional<repo::BinEntry> entry; // populated for BinRepo, nullopt for AstralSource
};

// Parse a constraint string like "python >= 3.11" or "curl-bin"
VersionConstraint parse_constraint(std::string_view raw);

// Resolve a package constraint:
//   <pkg>-bin  → must be in bin/ repo; throws if not found
//   <pkg>      → bin/ repo first (if found, BinRepo); otherwise AstralSource
ResolveResult resolve(
    const std::filesystem::path& repo_bin_dir,
    const VersionConstraint& constraint
);

} // namespace lock
