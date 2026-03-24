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
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d_%H:%M:%S");
    return ss.str();
}

std::string hash_blob(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount())
        EVP_DigestUpdate(ctx, buf, static_cast<std::size_t>(f.gcount()));
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream ss;
    ss << "sha256-";
    for (unsigned int i = 0; i < len; ++i)
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    return ss.str();
}

std::filesystem::path compress_path(const std::filesystem::path& source,
                                     const std::filesystem::path& workdir) {
    auto archive = workdir / "snap.tar.zst";
    std::string cmd;
    if (std::filesystem::is_directory(source)) {
        cmd = "tar cf - -C " + source.parent_path().string()
            + " " + source.filename().string()
            + " | zstd -T0 -19 -o " + archive.string();
    } else {
        cmd = "zstd -T0 -19 -o " + archive.string() + " " + source.string();
    }
    auto r = util::run("sh", {"-c", cmd});
    if (r.exit_code != 0)
        throw std::runtime_error("compression failed: " + r.stderr_output);
    return archive;
}

std::optional<std::string> json_field(const std::string& json, const std::string& field) {
    auto pos = json.find("\"" + field + "\"");
    if (pos == std::string::npos) return std::nullopt;
    auto c  = json.find(":", pos);  if (c  == std::string::npos) return std::nullopt;
    auto q1 = json.find('"', c);   if (q1 == std::string::npos) return std::nullopt;
    auto q2 = json.find('"', q1 + 1); if (q2 == std::string::npos) return std::nullopt;
    return json.substr(q1 + 1, q2 - q1 - 1);
}

} // anonymous namespace

bool has_zstd() {
    return util::run("sh", {"-c", "command -v zstd >/dev/null 2>&1"}).exit_code == 0;
}

SnapResult create(const std::filesystem::path& path,
                   const std::filesystem::path& store_root,
                   const std::filesystem::path& snap_index,
                   const std::string& reason) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error("path does not exist: " + path.string());
    if (!has_zstd())
        throw std::runtime_error("zstd required for snapshots: astral -S zstd");

    util::TempDir work;
    auto archive = compress_path(path, work.path());

    uint64_t orig = 0;
    if (std::filesystem::is_directory(path)) {
        for (auto& e : std::filesystem::recursive_directory_iterator(path))
            if (e.is_regular_file()) orig += e.file_size();
    } else {
        orig = std::filesystem::file_size(path);
    }
    uint64_t comp = std::filesystem::file_size(archive);

    std::string blob_hash = hash_blob(archive);
    auto blob_dir = store_root / blob_hash;
    bool deduped  = std::filesystem::exists(blob_dir / "data.zst");

    if (!deduped) {
        std::filesystem::create_directories(blob_dir);
        std::filesystem::copy_file(archive, blob_dir / "data.zst");
        auto perms = std::filesystem::status(path).permissions();
        std::ofstream meta(blob_dir / "meta.json");
        meta << "{\n"
             << "  \"original_path\": \"" << path.string() << "\",\n"
             << "  \"is_dir\": " << (std::filesystem::is_directory(path) ? "true" : "false") << ",\n"
             << "  \"size_bytes\": " << orig << ",\n"
             << "  \"mode\": \"" << std::oct << std::setfill('0') << std::setw(4)
                                 << (static_cast<unsigned>(perms) & 07777) << "\",\n"
             << "  \"snapshotted_at\": \"" << current_timestamp() << "\",\n"
             << "  \"astral_env_version\": \"" << config::version() << "\"\n"
             << "}\n";
    }

    std::filesystem::create_directories(snap_index);
    std::string id = "snap-" + current_timestamp();
    std::ofstream idx(snap_index / (id + ".json"));
    idx << "{\n"
        << "  \"id\": \""         << id        << "\",\n"
        << "  \"path\": \""       << path.string() << "\",\n"
        << "  \"blob\": \""       << blob_hash << "\",\n"
        << "  \"reason\": \""     << reason    << "\",\n"
        << "  \"created_at\": \"" << current_timestamp() << "\"\n"
        << "}\n";

    return {id, blob_hash, orig, comp, deduped};
}

std::optional<SnapInfo> get_snapshot(const std::string& id,
                                      const std::filesystem::path& snap_index) {
    auto f = snap_index / (id + ".json");
    if (!std::filesystem::exists(f)) return std::nullopt;
    std::string c = util::read_file(f);
    SnapInfo info;
    info.id = id;
    if (auto p = json_field(c, "path"))       info.path       = *p;
    if (auto p = json_field(c, "blob"))       info.blob       = *p;
    if (auto p = json_field(c, "reason"))     info.reason     = *p;
    if (auto p = json_field(c, "created_at")) info.created_at = *p;
    return info;
}

void restore(const std::string& id,
             const std::filesystem::path& snap_index,
             const std::filesystem::path& store_root,
             std::optional<std::filesystem::path> dest) {
    auto info = get_snapshot(id, snap_index);
    if (!info) throw std::runtime_error("snapshot not found: " + id);

    auto target  = dest.value_or(info->path);
    auto blob_dir = store_root / info->blob;
    auto archive  = blob_dir / "data.zst";
    if (!std::filesystem::exists(archive))
        throw std::runtime_error("blob not found: " + info->blob);

    if (std::filesystem::exists(target)) {
        auto bak = target.string() + ".bak-" + current_timestamp();
        std::filesystem::rename(target, bak);
        std::cout << "Backed up: " << bak << "\n";
    }
    std::filesystem::create_directories(target.parent_path());

    bool is_dir = true;
    auto meta_path = blob_dir / "meta.json";
    if (std::filesystem::exists(meta_path)) {
        auto mc = util::read_file(meta_path);
        if (auto p = json_field(mc, "is_dir")) is_dir = (*p == "true");
    }

    util::TempDir work;
    if (is_dir) {
        auto r = util::run("sh", {"-c",
            "zstd -d -c " + archive.string() + " | tar xf - -C " + target.parent_path().string()});
        if (r.exit_code != 0) throw std::runtime_error("restore failed: " + r.stderr_output);
    } else {
        auto r = util::run("sh", {"-c",
            "zstd -d -c " + archive.string() + " | tar xf - -C " + work.path().string()});
        if (r.exit_code != 0) throw std::runtime_error("restore failed: " + r.stderr_output);
        std::filesystem::path extracted;
        for (const auto& e : std::filesystem::directory_iterator(work.path()))
            if (e.is_regular_file()) { extracted = e.path(); break; }
        if (extracted.empty()) throw std::runtime_error("restore: no file after extraction");
        std::filesystem::rename(extracted, target);
    }
    std::cout << "Restored: " << target << "\n";
}

std::vector<SnapInfo> list(const std::filesystem::path& snap_index,
                            std::optional<std::filesystem::path> filter_path) {
    std::vector<SnapInfo> results;
    if (!std::filesystem::exists(snap_index)) return results;
    for (const auto& e : std::filesystem::directory_iterator(snap_index)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        auto stem = e.path().stem().string();
        if (stem.rfind("snap-", 0) != 0) continue;
        auto info = get_snapshot(stem, snap_index);
        if (!info) continue;
        if (filter_path && info->path != *filter_path) continue;
        results.push_back(*info);
    }
    std::sort(results.begin(), results.end(),
        [](const SnapInfo& a, const SnapInfo& b) { return a.created_at > b.created_at; });
    return results;
}

int prune(const std::filesystem::path& snap_index,
          const std::filesystem::path&,
          int older_than_days,
          int keep_last) {
    auto all = list(snap_index);
    int pruned = 0;
    std::map<std::string, std::vector<SnapInfo>> by_path;
    for (const auto& s : all) by_path[s.path.string()].push_back(s);

    auto now = std::chrono::system_clock::now();
    for (auto& entry : by_path) {
        const auto& snaps = entry.second;
        int kept = 0;
        for (const auto& snap : snaps) {
            bool remove = (keep_last > 0 && kept >= keep_last);
            if (!remove && older_than_days > 0) {
                std::tm tm = {};
                std::istringstream ss(snap.created_at);
                ss >> std::get_time(&tm, "%Y-%m-%d_%H:%M:%S");
                if (!ss.fail()) {
                    auto st  = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    auto age = std::chrono::duration_cast<std::chrono::hours>(now - st).count();
                    if (age >= older_than_days * 24) remove = true;
                }
            }
            if (remove) {
                std::error_code ec;
                std::filesystem::remove(snap_index / (snap.id + ".json"), ec);
                if (!ec) ++pruned;
            } else {
                ++kept;
            }
        }
    }
    return pruned;
}

} // namespace snap
