#include "store/entry.hpp"
#include "store/store.hpp"
#include "util/tempdir.hpp"
#include "util/process.hpp"
#include "util/file.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

namespace store {

namespace {

std::pair<std::string, std::string> split_checksum(const std::string& checksum) {
    auto colon_pos = checksum.find(':');
    if (colon_pos == std::string::npos) return {"sha256", checksum};
    return {checksum.substr(0, colon_pos), checksum.substr(colon_pos + 1)};
}

std::string compute_checksum(const std::string& algo, const std::filesystem::path& file) {
    std::string cmd;
    if      (algo == "sha256") cmd = "sha256sum";
    else if (algo == "sha512") cmd = "sha512sum";
    else if (algo == "md5")    cmd = "md5sum";
    else throw std::runtime_error("Unsupported checksum algorithm: " + algo);

    auto result = util::run(cmd, {file.string()});
    std::istringstream ss(result.stdout_output);
    std::string hash;
    ss >> hash;
    return hash;
}

void extract_tarball(const std::filesystem::path& archive, const std::filesystem::path& dest) {
    std::string ext = archive.extension().string();
    std::vector<std::string> args;

    if      (ext == ".gz"  || ext == ".tgz")  args = {"-xzf",  archive.string(), "-C", dest.string()};
    else if (ext == ".bz2" || ext == ".tbz2") args = {"-xjf",  archive.string(), "-C", dest.string()};
    else if (ext == ".xz"  || ext == ".txz")  args = {"-xJf",  archive.string(), "-C", dest.string()};
    else                                        args = {"-xf",   archive.string(), "-C", dest.string()};

    auto result = util::run("tar", args);
    if (result.exit_code != 0)
        throw std::runtime_error("Failed to extract tarball: " + result.stderr_output);
}

} // anonymous namespace

std::expected<void, InstallError> install_prebuilt(
    const LockEntry& entry,
    const std::filesystem::path& store_path
) {
    util::TempDir work;
    auto archive = work.path() / "source.tarball";

    std::cout << "Fetching " << entry.name << " " << entry.version << "...\n";
    auto result = util::run("curl", {"-L", "-o", archive.string(), entry.url});
    if (result.exit_code != 0) {
        std::cerr << "Download failed: " << result.stderr_output << "\n";
        return std::unexpected(InstallError::NetworkError);
    }

    auto [algo, expected] = split_checksum(entry.checksum);
    if (compute_checksum(algo, archive) != expected) {
        std::cerr << "Checksum mismatch for " << entry.name << "\n";
        return std::unexpected(InstallError::ChecksumMismatch);
    }

    std::filesystem::create_directories(store_path);
    try { extract_tarball(archive, store_path); }
    catch (const std::exception& e) {
        std::cerr << "Extraction failed: " << e.what() << "\n";
        std::filesystem::remove_all(store_path);
        return std::unexpected(InstallError::ExtractionFailed);
    }

    mark_complete(store_path);
    std::cout << "Installed " << entry.name << " " << entry.version << " → " << store_path << "\n";
    return {};
}

std::expected<void, InstallError> install_source(
    const LockEntry& entry,
    const std::filesystem::path& store_path,
    int max_jobs
) {
    util::TempDir work;
    auto archive = work.path() / "source.tarball";

    std::cout << "Fetching " << entry.name << " " << entry.version << " (source build)...\n";
    auto result = util::run("curl", {"-L", "-o", archive.string(), entry.url});
    if (result.exit_code != 0) return std::unexpected(InstallError::NetworkError);

    auto [algo, expected] = split_checksum(entry.checksum);
    if (compute_checksum(algo, archive) != expected)
        return std::unexpected(InstallError::ChecksumMismatch);

    std::filesystem::create_directories(store_path);
    try { extract_tarball(archive, work.path()); }
    catch (...) { return std::unexpected(InstallError::ExtractionFailed); }

    // Find extracted source directory
    std::filesystem::path source_dir;
    for (const auto& d : std::filesystem::directory_iterator(work.path()))
        if (d.is_directory()) { source_dir = d.path(); break; }
    if (source_dir.empty()) return std::unexpected(InstallError::ExtractionFailed);

    std::cout << "Building " << entry.name << "...\n";
    std::vector<std::pair<std::string, std::string>> env = {{"ENVDIR", store_path.string()}};

    if (entry.build_script && !entry.build_script->empty()) {
        std::string script = *entry.build_script;
        size_t pos = 0;
        while ((pos = script.find("$ENVDIR", pos)) != std::string::npos) {
            script.replace(pos, 7, store_path.string());
            pos += store_path.string().length();
        }
        result = util::run("sh", {"-c", script}, source_dir, env);
        if (result.exit_code != 0) {
            std::cerr << "Build failed: " << result.stderr_output << "\n";
            return std::unexpected(InstallError::BuildFailed);
        }
    } else {
        // Autotools default, CMake fallback
        result = util::run("./configure", {"--prefix=" + store_path.string()}, source_dir, env);
        if (result.exit_code != 0) {
            result = util::run("cmake", {"-B", "build",
                "-DCMAKE_INSTALL_PREFIX=" + store_path.string()}, source_dir, env);
            if (result.exit_code != 0) return std::unexpected(InstallError::BuildFailed);
            result = util::run("cmake", {"--build", "build", "--",
                "-j" + std::to_string(max_jobs)}, source_dir, env);
            if (result.exit_code != 0) return std::unexpected(InstallError::BuildFailed);
            result = util::run("cmake", {"--install", "build"}, source_dir, env);
            if (result.exit_code != 0) return std::unexpected(InstallError::BuildFailed);
        } else {
            result = util::run("make", {"-j" + std::to_string(max_jobs)}, source_dir, env);
            if (result.exit_code != 0) return std::unexpected(InstallError::BuildFailed);
            result = util::run("make", {"install"}, source_dir, env);
            if (result.exit_code != 0) return std::unexpected(InstallError::BuildFailed);
        }
    }

    mark_complete(store_path);
    std::cout << "Built and installed " << entry.name << " " << entry.version
              << " → " << store_path << "\n";
    return {};
}

std::expected<void, InstallError> install_via_astral(const LockEntry& entry) {
    std::cout << "Installing " << entry.name;
    if (!entry.version.empty()) std::cout << " " << entry.op << " " << entry.version;
    std::cout << " via astral...\n";

    std::vector<std::string> args = {"-y", "-S", entry.name};
    auto result = util::run("astral", args);
    if (result.exit_code != 0) {
        std::cerr << "astral failed for " << entry.name << ": " << result.stderr_output << "\n";
        return std::unexpected(InstallError::BuildFailed);
    }

    std::cout << "Installed " << entry.name << " (host)\n";
    return {};
}

std::expected<void, InstallError> install(
    const LockEntry& entry,
    const std::filesystem::path& store_root,
    int max_jobs,
    bool force
) {
    // AstralSource entries are delegated to astral -S (host install, not store)
    if (entry.build_kind == lock::BuildKind::AstralSource) {
        return install_via_astral(entry);
    }

    auto path = store::store_path(store_root, entry.name, entry.version, entry.checksum);

    if (!force && entry_exists(path)) {
        std::cout << entry.name << " " << entry.version << " already in store, skipping\n";
        return {};
    }
    if (force && entry_exists(path)) {
        std::cout << "Force reinstalling " << entry.name << "...\n";
        std::filesystem::remove_all(path);
    }

    std::filesystem::create_directories(store_root);

    return (entry.build_kind == lock::BuildKind::Source)
        ? install_source(entry, path, max_jobs)
        : install_prebuilt(entry, path);
}

std::vector<std::filesystem::path> list_store_entries(const std::filesystem::path& store_root) {
    std::vector<std::filesystem::path> entries;
    if (!std::filesystem::exists(store_root)) return entries;
    for (const auto& d : std::filesystem::directory_iterator(store_root))
        if (d.is_directory() && entry_exists(d.path())) entries.push_back(d.path());
    return entries;
}

uint64_t store_size(const std::filesystem::path& store_root) {
    uint64_t total = 0;
    for (const auto& entry : list_store_entries(store_root))
        for (const auto& f : std::filesystem::recursive_directory_iterator(entry))
            if (f.is_regular_file()) total += f.file_size();
    return total;
}

} // namespace store
