#include "gc/gc.hpp"
#include "store/store.hpp"
#include "store/entry.hpp"
#include "util/file.hpp"

#include <iostream>
#include <algorithm>
#include <set>

namespace gc {

namespace {

std::optional<std::string> json_field(const std::string& json, const std::string& field) {
    auto pos = json.find("\"" + field + "\"");
    if (pos == std::string::npos) return std::nullopt;
    auto c  = json.find(":", pos);  if (c  == std::string::npos) return std::nullopt;
    auto q1 = json.find('"', c);   if (q1 == std::string::npos) return std::nullopt;
    auto q2 = json.find('"', q1 + 1); if (q2 == std::string::npos) return std::nullopt;
    return json.substr(q1 + 1, q2 - q1 - 1);
}

std::set<std::string> collect_snap_refs(
    const std::filesystem::path& snap_index = "/astral-env/snapshots/files") {
    std::set<std::string> refs;
    if (!std::filesystem::exists(snap_index)) return refs;
    for (const auto& e : std::filesystem::directory_iterator(snap_index)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        try {
            auto content = util::read_file(e.path());
            if (auto p = json_field(content, "blob")) refs.insert(*p);
        } catch (...) {}
    }
    return refs;
}

} // anonymous namespace

std::vector<std::filesystem::path> find_lockfiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(root)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().filename() == "astral-env.lock")
            out.push_back(e.path());
    }
    return out;
}

bool is_referenced(const std::filesystem::path& entry_path,
                   const std::vector<std::filesystem::path>& lockfiles) {
    std::string ep = entry_path.string();
    for (const auto& lf : lockfiles) {
        try {
            if (util::read_file(lf).find(ep) != std::string::npos) return true;
        } catch (...) {}
    }
    return false;
}

std::vector<GcEntry> find_candidates_multi(
    const std::filesystem::path& store_root,
    const std::vector<std::filesystem::path>& lockfiles,
    int gc_keep_days) {
    std::vector<GcEntry> out;
    if (!std::filesystem::exists(store_root)) return out;

    auto snap_refs = collect_snap_refs();
    auto now    = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::days(gc_keep_days);

    for (const auto& e : std::filesystem::directory_iterator(store_root)) {
        if (!e.is_directory()) continue;
        auto ep = e.path();
        if (ep.filename() == "snap") continue;
        if (snap_refs.count(ep.filename().string())) continue;
        if (!store::entry_exists(ep)) continue;
        auto ct = store::get_completion_time(ep);
        if (ct && *ct > cutoff) continue;
        if (is_referenced(ep, lockfiles)) continue;

        uint64_t sz = 0;
        for (const auto& f : std::filesystem::recursive_directory_iterator(ep))
            if (f.is_regular_file()) sz += f.file_size();
        out.push_back({ep, ct.value_or(now), sz});
    }
    return out;
}

std::vector<GcEntry> find_candidates(
    const std::filesystem::path& store_root,
    const std::filesystem::path& search_root,
    int gc_keep_days) {
    return find_candidates_multi(store_root, find_lockfiles(search_root), gc_keep_days);
}

uint64_t collect(const std::filesystem::path&,
                 const std::vector<GcEntry>& candidates) {
    uint64_t freed = 0;
    for (const auto& c : candidates) {
        std::cout << "Removing " << c.path.filename().string()
                  << " (" << (c.size / 1024 / 1024) << " MB)\n";
        try {
            std::filesystem::remove_all(c.path);
            freed += c.size;
        } catch (const std::exception& ex) {
            std::cerr << "Failed to remove " << c.path << ": " << ex.what() << "\n";
        }
    }
    return freed;
}

} // namespace gc
