#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace lock {

// How the package was / will be obtained
enum class BuildKind {
    Source,       // Built from source tarball via build_script in bin/ entry
                  // → installed into content-addressed store
    Prebuilt,     // Pre-built binary tarball from bin/ entry (no build_script)
                  // → extracted into content-addressed store
    AstralSource, // Not in bin/ repo — fetched via astral's recipe system at build time
                  // → astral -S <pkg> installs to host system, not the store
                  // Use <pkg>-bin to get a store-isolated prebuilt instead.
};

struct LockEntry {
    std::string name;
    std::string version;   // empty for AstralSource with unconstrained version
    std::string url;       // empty for AstralSource
    std::string checksum;  // empty for AstralSource
    std::filesystem::path store_path; // empty for AstralSource
    BuildKind build_kind;
    std::optional<std::string> build_script; // Source builds only
};

struct Lockfile {
    std::vector<LockEntry> entries;
    std::string generated_at;
    std::string astral_env_version;
};

// Read a lockfile from disk
Lockfile read(const std::filesystem::path& lock_path);

// Write a lockfile to disk
void write(const Lockfile& lock, const std::filesystem::path& lock_path);

// Generate a lockfile from an astral-env.stars file.
//
// Resolution rules applied per package name:
//   <pkg>      → AstralSource: astral -S at build time (default, source build)
//   <pkg>-bin  → BinRepo: must exist in AOHARU bin/ (prebuilt or source recipe)
//
// Throws std::runtime_error if a -bin request can't be satisfied.
Lockfile generate(
    const std::filesystem::path& stars_path,
    const std::filesystem::path& repo_bin_dir,
    bool update_all = false,
    const std::vector<std::string>& update_packages = {}
);

} // namespace lock
