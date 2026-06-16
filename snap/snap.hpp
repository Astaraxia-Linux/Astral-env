#pragma once

#include <filesystem>
#include <string>
#include <optional>
#include <chrono>
#include <vector>

namespace snap {

inline constexpr const char* SNAP_STORE = "/astral-env/store/snap";
inline constexpr const char* SNAP_INDEX = "/astral-env/snapshots/files";

struct SnapResult {
    std::string snapshot_id;      // e.g. "snap-2026-03-06_14:32:00"
    std::string blob_hash;        // sha256-<64hex>
    uint64_t    original_bytes;
    uint64_t    compressed_bytes;
    bool        deduped;          // true if blob already existed (file unchanged)
};

struct SnapInfo {
    std::string id;
    std::filesystem::path path;
    std::string blob;
    std::string reason;
    std::string created_at;
};

// Create a snapshot of path (file or directory).
SnapResult create(
    const std::filesystem::path& path,
    const std::filesystem::path& store_root = SNAP_STORE,
    const std::filesystem::path& snap_index = SNAP_INDEX,
    const std::string&           reason     = "manual"
);

// Restore a snapshot by ID. Backs up whatever exists at dest before overwriting.
void restore(
    const std::string&                    snapshot_id,
    const std::filesystem::path&          snap_index  = SNAP_INDEX,
    const std::filesystem::path&          store_root  = SNAP_STORE,
    std::optional<std::filesystem::path>  dest        = std::nullopt
);

// List snapshot IDs, optionally filtered to a specific original path.
std::vector<SnapInfo> list(
    const std::filesystem::path&          snap_index,
    std::optional<std::filesystem::path>  filter_path = std::nullopt
);

// Get info about a specific snapshot
std::optional<SnapInfo> get_snapshot(
    const std::string&            snapshot_id,
    const std::filesystem::path&  snap_index = SNAP_INDEX
);

// Prune snapshots
int prune(
    const std::filesystem::path& snap_index,
    const std::filesystem::path& store_root,
    int older_than_days = -1,  // -1 = no age limit
    int keep_last = -1         // -1 = no limit
);

// Check if zstd is available
bool has_zstd();

} // namespace snap
