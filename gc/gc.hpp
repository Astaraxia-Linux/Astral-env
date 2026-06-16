#pragma once

#include <filesystem>
#include <vector>
#include <cstdint>
#include <chrono>

namespace gc {

struct GcEntry {
    std::filesystem::path path;
    std::chrono::system_clock::time_point last_used;
    uint64_t size;
};

// Find entries that are candidates for garbage collection
// search_root is where to look for lockfiles (e.g., "/home" for user projects)
std::vector<GcEntry> find_candidates(
    const std::filesystem::path& store_root,
    const std::filesystem::path& search_root,
    int gc_keep_days = 30
);

// Collect (delete) GC candidates
// Returns total bytes freed
uint64_t collect(
    const std::filesystem::path& store_root,
    const std::vector<GcEntry>& candidates
);

// Get all lockfiles in a directory tree
std::vector<std::filesystem::path> find_lockfiles(
    const std::filesystem::path& search_root
);

// Find candidates with pre-collected lockfiles (avoids searching multiple roots)
std::vector<GcEntry> find_candidates_multi(
    const std::filesystem::path& store_root,
    const std::vector<std::filesystem::path>& lockfiles,
    int gc_keep_days = 30
);

// Check if a store entry is referenced by any known lockfile
bool is_referenced(
    const std::filesystem::path& entry_path,
    const std::vector<std::filesystem::path>& lockfiles
);

} // namespace gc
