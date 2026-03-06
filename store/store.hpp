#pragma once

#include <filesystem>
#include <string>

namespace store {

// Compute the full SHA-256 store hash from package identity
// sha256(name + "-" + version + "-" + source_checksum) → "sha256-<64hex>"
std::string compute_hash(
    std::string_view name,
    std::string_view version,
    std::string_view source_checksum
);

// Full store path for a package
// e.g. /astral-env/store/sha256-<64hex>-python-3.12.4
std::filesystem::path store_path(
    const std::filesystem::path& store_root,
    std::string_view name,
    std::string_view version,
    std::string_view source_checksum
);

// Check if a store entry exists and is complete (has a .complete marker)
bool entry_exists(const std::filesystem::path& entry_path);

// Mark a store entry as complete (called after successful install)
void mark_complete(const std::filesystem::path& entry_path);

// Get the timestamp of when entry was completed
std::optional<std::chrono::system_clock::time_point> get_completion_time(
    const std::filesystem::path& entry_path
);

} // namespace store