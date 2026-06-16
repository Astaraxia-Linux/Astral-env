#include "gc/gc.hpp"
#include "store/store.hpp"
#include "store/entry.hpp"
#include "util/file.hpp"
#include <iostream>
#include <algorithm>
#include <regex>
#include <set>

namespace gc {

namespace {

// Collect all blob hashes referenced by the snap index
std::set<std::string> collect_snap_live_refs(
    const std::filesystem::path& snap_index = "/astral-env/snapshots/files"
) {
    std::set<std::string> refs;
    if (!std::filesystem::exists(snap_index)) return refs;

    for (const auto& entry : std::filesystem::directory_iterator(snap_index)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        try {
            std::string content = util::read_file(entry.path());
            // Extract "blob": "sha256-..." field
            auto pos = content.find("\"blob\"");
            if (pos == std::string::npos) continue;
            auto q1 = content.find('"', pos + 6);
            if (q1 == std::string::npos) continue;
            q1 = content.find('"', q1 + 1); // skip the colon-space, find opening quote of value
            if (q1 == std::string::npos) continue;
            auto q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            refs.insert(content.substr(q1 + 1, q2 - q1 - 1));
        } catch (...) {}
    }
    return refs;
}

} // namespace

std::vector<std::filesystem::path> find_lockfiles(const std::filesystem::path& search_root) {
    std::vector<std::filesystem::path> lockfiles;
    
    if (!std::filesystem::exists(search_root)) {
        return lockfiles;
    }
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(search_root)) {
        if (entry.is_regular_file()) {
            auto filename = entry.path().filename().string();
            if (filename == "astral-env.lock") {
                lockfiles.push_back(entry.path());
            }
        }
    }
    
    return lockfiles;
}

bool is_referenced(
    const std::filesystem::path& entry_path,
    const std::vector<std::filesystem::path>& lockfiles
) {
    // Use the full store path string for unambiguous matching
    // This avoids false positives like "gcc" matching "gcc-libs"
    std::string entry_path_str = entry_path.string();
    
    for (const auto& lockfile : lockfiles) {
        try {
            std::string content = util::read_file(lockfile);
            
            // Search for the full store path ( unambiguous)
            if (content.find(entry_path_str) != std::string::npos) {
                return true;
            }
        } catch (...) {
            // Skip unreadable lockfiles
        }
    }
    
    return false;
}

std::vector<GcEntry> find_candidates(
    const std::filesystem::path& store_root,
    const std::filesystem::path& search_root,
    int gc_keep_days
) {
    std::vector<GcEntry> candidates;
    
    if (!std::filesystem::exists(store_root)) {
        return candidates;
    }
    
    // Collect lockfiles first - this is critical for safety
    auto lockfiles = find_lockfiles(search_root);

    // Protect snap blobs from GC
    auto snap_refs = collect_snap_live_refs();

    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::days(gc_keep_days);
    
    // Find all store entries
    for (const auto& entry : std::filesystem::directory_iterator(store_root)) {
        if (!entry.is_directory()) continue;
        
        auto entry_path = entry.path();

        // Skip the snap/ subdirectory entirely — snap GC is handled separately
        if (entry_path.filename() == "snap") continue;

        // Skip incomplete entries
        if (!store::entry_exists(entry_path)) continue;
        
        // Get completion time
        auto completion_time = store::get_completion_time(entry_path);
        
        // Skip entries that are too new
        if (completion_time && *completion_time > cutoff) continue;
        
        // CRITICAL FIX: Skip entries that are referenced by any lockfile
        if (is_referenced(entry_path, lockfiles)) continue;
        
        // Calculate size
        uint64_t size = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(entry_path)) {
            if (file.is_regular_file()) {
                size += file.file_size();
            }
        }
        
        candidates.push_back({
            entry_path,
            completion_time.value_or(now),
            size
        });
    }
    
    return candidates;
}

std::vector<GcEntry> find_candidates_multi(
    const std::filesystem::path& store_root,
    const std::vector<std::filesystem::path>& lockfiles,
    int gc_keep_days
) {
    std::vector<GcEntry> candidates;
    
    if (!std::filesystem::exists(store_root)) {
        return candidates;
    }
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::days(gc_keep_days);

    // Protect snap blobs from GC
    auto snap_refs = collect_snap_live_refs();
    
    // Find all store entries
    for (const auto& entry : std::filesystem::directory_iterator(store_root)) {
        if (!entry.is_directory()) continue;
        
        auto entry_path = entry.path();

        // Skip the snap/ subdirectory entirely — snap GC is handled separately
        if (entry_path.filename() == "snap") continue;

        // Skip incomplete entries
        if (!store::entry_exists(entry_path)) continue;

        // Get completion time
        auto completion_time = store::get_completion_time(entry_path);

        // Skip entries that are too new
        if (completion_time && *completion_time > cutoff) continue;

        // Skip entries referenced by any lockfile
        if (is_referenced(entry_path, lockfiles)) continue;

        // Calculate size
        uint64_t size = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(entry_path)) {
            if (file.is_regular_file()) {
                size += file.file_size();
            }
        }

        candidates.push_back({
            entry_path,
            completion_time.value_or(now),
            size
        });
    }
    
    return candidates;
}

uint64_t collect(
    const std::filesystem::path& store_root,
    const std::vector<GcEntry>& candidates
) {
    uint64_t freed = 0;
    
    for (const auto& candidate : candidates) {
        std::cout << "Removing " << candidate.path.filename().string() << " ("
                  << (candidate.size / 1024 / 1024) << " MB)\n";
        
        try {
            std::filesystem::remove_all(candidate.path);
            freed += candidate.size;
        } catch (const std::exception& e) {
            std::cerr << "Failed to remove " << candidate.path.string() << ": " << e.what() << "\n";
        }
    }
    
    return freed;
}

} // namespace gc