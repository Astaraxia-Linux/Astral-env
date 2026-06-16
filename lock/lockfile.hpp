#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace lock {

// Build kind: how the package was built
enum class BuildKind {
    Source,    // built from source via $ENV.Build
    Prebuilt,  // pre-built binary tarball
    System,    // installed via astral -S into /usr (not in store)
};

struct LockEntry {
    std::string name;
    std::string version;
    std::string url;
    std::string checksum;
    std::filesystem::path store_path;
    BuildKind build_kind;  // replaced source_built bool with enum
    std::optional<std::string> build_script;  // for custom build commands
};

struct Lockfile {
    std::vector<LockEntry> entries;
    std::string generated_at;
    std::string astral_env_version;
};

// Read a lockfile
Lockfile read(const std::filesystem::path& lock_path);

// Write a lockfile
void write(const Lockfile& lock, const std::filesystem::path& lock_path);

// Generate a lockfile from an astral-env.stars file
// Resolves version constraints against the /bin repo
Lockfile generate(
    const std::filesystem::path& stars_path,
    const std::filesystem::path& repo_bin_dir,
    bool update_all = false,
    const std::vector<std::string>& update_packages = {}
);

} // namespace lock
