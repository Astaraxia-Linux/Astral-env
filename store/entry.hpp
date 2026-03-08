#pragma once

#include "lock/lockfile.hpp"
#include <filesystem>
#include <string>
#include <optional>
#include <expected>
#include <vector>
#include <cstdint>

namespace store {

enum class InstallError {
    ChecksumMismatch,
    BuildFailed,
    ExtractionFailed,
    DiskError,
    NetworkError,
};

// Use LockEntry and BuildKind from lock/lockfile.hpp
using LockEntry = lock::LockEntry;

// Install a lock entry.
//   Source / Prebuilt  → downloaded and installed into content-addressed store
//   AstralSource       → delegated to `astral -S <pkg>` (installs to host /usr)
// Idempotent: skips store entries whose .complete marker already exists (unless force=true).
std::expected<void, InstallError> install(
    const LockEntry& entry,
    const std::filesystem::path& store_root,
    int max_jobs = 4,
    bool force = false
);

// Install a pre-built tarball entry into the store
std::expected<void, InstallError> install_prebuilt(
    const LockEntry& entry,
    const std::filesystem::path& store_path
);

// Build from source tarball into the store
std::expected<void, InstallError> install_source(
    const LockEntry& entry,
    const std::filesystem::path& store_path,
    int max_jobs
);

// Delegate install to astral -S (for AstralSource entries)
std::expected<void, InstallError> install_via_astral(
    const LockEntry& entry
);

// List all complete store entries
std::vector<std::filesystem::path> list_store_entries(const std::filesystem::path& store_root);

// Get total store size in bytes
uint64_t store_size(const std::filesystem::path& store_root);

} // namespace store
