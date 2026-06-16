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

// Install a lock entry to the store.
// Idempotent — skips if store_path/.complete exists and checksum matches.
// Returns Err on checksum failure or build failure.
// Throws std::runtime_error on unrecoverable disk error.
std::expected<void, InstallError> install(
    const LockEntry& entry,
    const std::filesystem::path& store_root,
    int max_jobs = 4,
    bool force = false
);

// Install a pre-built tarball entry
std::expected<void, InstallError> install_prebuilt(
    const LockEntry& entry,
    const std::filesystem::path& store_path
);

// Install a source-built entry (runs $ENV.Build)
std::expected<void, InstallError> install_source(
    const LockEntry& entry,
    const std::filesystem::path& store_path,
    int max_jobs
);

// List all store entries
std::vector<std::filesystem::path> list_store_entries(const std::filesystem::path& store_root);

// Get total store size in bytes
uint64_t store_size(const std::filesystem::path& store_root);

} // namespace store
