#include "snap/snap.hpp"
#include "util/file.hpp"
#include "util/process.hpp"
#include "util/tempdir.hpp"
#include "config/conf.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <iostream>
#include <openssl/evp.h>

namespace snap {

namespace {

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H:%M:%S");
    return ss.str();
}

std::string hash_blob(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount())
        EVP_DigestUpdate(ctx, buf, (size_t)f.gcount());

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream ss;
    ss << "sha256-";
    for (unsigned int i = 0; i < digest_len; i++)
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];
    return ss.str();
}

std::filesystem::path compress_path(
    const std::filesystem::path& source,
    const std::filesystem::path& workdir
) {
    auto archive = workdir / "snap.tar.zst";
    
    // Check if source is file or directory
    bool is_dir = std::filesystem::is_directory(source);
    
    std::string cmd;
    if (is_dir) {
        // Use tar with zstd for directories
        cmd = "tar cf - -C " + source.parent_path().string() 
            + " " + source.filename().string() 
            + " | zstd -T0 -19 -o " + archive.string();
    } else {
        // For single files, just compress directly
        cmd = "zstd -T0 -19 -o " + archive.string() + " " + source.string();
    }
    
    auto result = util::run("bash", {"-c", cmd});
    if (result.exit_code != 0) {
        throw std::runtime_error("Compression failed: " + result.stderr_output);
    }
    return archive;
}

std::optional<std::string> extract_json_field(const std::string& json, const std::string& field) {
    auto pos = json.find("\"" + field + "\"");
    if (pos == std::string::npos) return std::nullopt;
    
    auto colon = json.find(":", pos);
    if (colon == std::string::npos) return std::nullopt;
    
    auto start = json.find("\"", colon);
    if (start == std::string::npos) return std::nullopt;
    
    auto end = json.find("\"", start + 1);
    if (end == std::string::npos) return std::nullopt;
    
    return json.substr(start + 1, end - start - 1);
}

} // anonymous namespace

bool has_zstd() {
    auto result = util::run("which", {"zstd"});
    return result.exit_code == 0;
}

SnapResult create(
    const std::filesystem::path& path,
    const std::filesystem::path& store_root,
    const std::filesystem::path& snap_index,
    const std::string& reason
) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Path does not exist: " + path.string());
    }
    
    // Check for zstd
    if (!has_zstd()) {
        throw std::runtime_error("zstd is required for snapshots. Install with: astral -S zstd");
    }
    
    util::TempDir work;
    auto archive = compress_path(path, work.path());

    uint64_t original_bytes = 0;
    if (std::filesystem::is_directory(path)) {
        for (auto& e : std::filesystem::recursive_directory_iterator(path))
            if (e.is_regular_file()) original_bytes += e.file_size();
    } else {
        original_bytes = std::filesystem::file_size(path);
    }
    
    uint64_t compressed_bytes = std::filesystem::file_size(archive);

    std::string blob_hash = hash_blob(archive);
    auto blob_dir = store_root / blob_hash;
    bool deduped = std::filesystem::exists(blob_dir / "data.zst");

    if (!deduped) {
        std::filesystem::create_directories(blob_dir);
        std::filesystem::copy_file(archive, blob_dir / "data.zst");

        // Get uid/gid and permissions
        auto perms = std::filesystem::status(path).permissions();
        
        std::ofstream meta(blob_dir / "meta.json");
        meta << "{\n"
             << "  \"original_path\": \"" << path.string() << "\",\n"
             << "  \"is_dir\": " << (std::filesystem::is_directory(path) ? "true" : "false") << ",\n"
             << "  \"size_bytes\": " << original_bytes << ",\n"
             << "  \"mode\": \"" << std::oct << std::setfill('0') << std::setw(4) << (static_cast<unsigned>(perms) & 07777) << "\",\n"
             << "  \"snapshotted_at\": \"" << current_timestamp() << "\",\n"
             << "  \"astral_env_version\": \"" << config::version() << "\"\n"
             << "}\n";
    }

    std::filesystem::create_directories(snap_index);
    std::string snap_id = "snap-" + current_timestamp();
    std::ofstream idx(snap_index / (snap_id + ".json"));
    idx << "{\n"
        << "  \"id\": \"" << snap_id << "\",\n"
        << "  \"path\": \"" << path.string() << "\",\n"
        << "  \"blob\": \"" << blob_hash << "\",\n"
        << "  \"reason\": \"" << reason << "\",\n"
        << "  \"created_at\": \"" << current_timestamp() << "\"\n"
        << "}\n";

    return SnapResult{snap_id, blob_hash, original_bytes, compressed_bytes, deduped};
}

std::optional<SnapInfo> get_snapshot(
    const std::string& snapshot_id,
    const std::filesystem::path& snap_index
) {
    auto snap_file = snap_index / (snapshot_id + ".json");
    if (!std::filesystem::exists(snap_file)) {
        return std::nullopt;
    }
    
    std::string content = util::read_file(snap_file);
    
    SnapInfo info;
    info.id = snapshot_id;
    
    auto path = extract_json_field(content, "path");
    if (path) info.path = *path;
    
    auto blob = extract_json_field(content, "blob");
    if (blob) info.blob = *blob;
    
    auto reason = extract_json_field(content, "reason");
    if (reason) info.reason = *reason;
    
    auto created = extract_json_field(content, "created_at");
    if (created) info.created_at = *created;
    
    return info;
}

void restore(
    const std::string& snapshot_id,
    const std::filesystem::path& snap_index,
    const std::filesystem::path& store_root,
    std::optional<std::filesystem::path> dest
) {
    auto info = get_snapshot(snapshot_id, snap_index);
    if (!info) {
        throw std::runtime_error("Snapshot not found: " + snapshot_id);
    }
    
    auto target = dest.value_or(info->path);
    auto blob_dir = store_root / info->blob;
    auto archive = blob_dir / "data.zst";
    
    if (!std::filesystem::exists(archive)) {
        throw std::runtime_error("Snapshot blob not found: " + info->blob);
    }
    
    // If target exists, back it up
    if (std::filesystem::exists(target)) {
        auto backup = target.string() + ".bak-" + current_timestamp();
        std::filesystem::rename(target, backup);
        std::cout << "Backed up existing file to: " << backup << "\n";
    }
    
    // Create parent directory
    std::filesystem::create_directories(target.parent_path());

    // Read meta to know if this was a file or directory
    bool is_dir = true;
    auto meta_path = blob_dir / "meta.json";
    if (std::filesystem::exists(meta_path)) {
        std::string meta_content = util::read_file(meta_path);
        auto is_dir_field = extract_json_field(meta_content, "is_dir");
        if (is_dir_field && *is_dir_field == "false") {
            is_dir = false;
        }
    }

    util::TempDir work;

    if (is_dir) {
        // Decompress tar.zst directly into parent — tar preserves the directory name
        auto result = util::run("bash", {"-c",
            "zstd -d -c " + archive.string()
            + " | tar xf - -C " + target.parent_path().string()
        });
        if (result.exit_code != 0) {
            throw std::runtime_error("Restore failed: " + result.stderr_output);
        }
    } else {
        // Single file: the tar contains one file named as the original basename.
        // Extract into workdir, then move to the exact target path.
        auto result = util::run("bash", {"-c",
            "zstd -d -c " + archive.string()
            + " | tar xf - -C " + work.path().string()
        });
        if (result.exit_code != 0) {
            throw std::runtime_error("Restore failed: " + result.stderr_output);
        }

        // Find the extracted file (there should be exactly one)
        std::filesystem::path extracted;
        for (const auto& e : std::filesystem::directory_iterator(work.path())) {
            if (e.is_regular_file()) { extracted = e.path(); break; }
        }
        if (extracted.empty()) {
            throw std::runtime_error("Restore: no file found after extraction");
        }
        std::filesystem::rename(extracted, target);
    }
    
    std::cout << "Restored: " << target << "\n";
}

std::vector<SnapInfo> list(
    const std::filesystem::path& snap_index,
    std::optional<std::filesystem::path> filter_path
) {
    std::vector<SnapInfo> results;
    
    if (!std::filesystem::exists(snap_index)) {
        return results;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(snap_index)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        
        auto filename = entry.path().stem().string();
        if (filename.rfind("snap-", 0) != 0) continue;
        
        auto info = get_snapshot(filename, snap_index);
        if (!info) continue;
        
        // Apply filter
        if (filter_path && info->path != *filter_path) {
            continue;
        }
        
        results.push_back(*info);
    }
    
    // Sort by creation time (newest first)
    std::sort(results.begin(), results.end(), 
        [](const SnapInfo& a, const SnapInfo& b) {
            return a.created_at > b.created_at;
        });
    
    return results;
}

int prune(
    const std::filesystem::path& snap_index,
    const std::filesystem::path& store_root,
    int older_than_days,
    int keep_last
) {
    auto all = list(snap_index);
    int pruned = 0;

    // Group by path — list() already returns newest-first
    std::map<std::string, std::vector<SnapInfo>> by_path;
    for (const auto& s : all) {
        by_path[s.path.string()].push_back(s);
    }

    auto now = std::chrono::system_clock::now();

    for (auto& [path_str, snaps] : by_path) {
        // snaps is newest-first
        int kept = 0;
        for (const auto& snap : snaps) {
            bool remove = false;

            // keep_last: remove once we've kept enough
            if (keep_last > 0 && kept >= keep_last) {
                remove = true;
            }

            // older_than_days: remove if the snapshot is old enough
            if (!remove && older_than_days > 0) {
                // Parse created_at: format is "%Y-%m-%d_%H:%M:%S"
                std::tm tm = {};
                std::istringstream ss(snap.created_at);
                ss >> std::get_time(&tm, "%Y-%m-%d_%H:%M:%S");
                if (!ss.fail()) {
                    auto snap_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    auto age = std::chrono::duration_cast<std::chrono::hours>(now - snap_time).count();
                    if (age >= older_than_days * 24) {
                        remove = true;
                    }
                }
            }

            if (remove) {
                auto snap_file = snap_index / (snap.id + ".json");
                std::error_code ec;
                std::filesystem::remove(snap_file, ec);
                if (!ec) pruned++;
            } else {
                kept++;
            }
        }
    }

    return pruned;
}

} // namespace snap