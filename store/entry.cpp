#include "store/entry.hpp"
#include "store/store.hpp"
#include "util/tempdir.hpp"
#include "util/process.hpp"
#include "util/file.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

namespace store {

namespace {

std::pair<std::string, std::string> split_checksum(const std::string& checksum) {
    auto colon_pos = checksum.find(':');
    if (colon_pos == std::string::npos) {
        return {"sha256", checksum};
    }
    return {checksum.substr(0, colon_pos), checksum.substr(colon_pos + 1)};
}

std::string compute_checksum(const std::string& algo, const std::filesystem::path& file) {
    std::string cmd;
    if (algo == "sha256") {
        cmd = "sha256sum";
    } else if (algo == "sha512") {
        cmd = "sha512sum";
    } else if (algo == "md5") {
        cmd = "md5sum";
    } else {
        throw std::runtime_error("Unsupported checksum algorithm: " + algo);
    }
    
    auto result = util::run(cmd, {file.string()});
    
    // Parse output: "hash  filename"
    std::istringstream ss(result.stdout_output);
    std::string hash;
    ss >> hash;
    
    return hash;
}

void extract_tarball(const std::filesystem::path& archive, const std::filesystem::path& dest) {
    std::string ext = archive.extension().string();
    std::vector<std::string> args;
    
    if (ext == ".gz" || ext == ".tgz") {
        args = {"-xzf", archive.string(), "-C", dest.string()};
    } else if (ext == ".bz2" || ext == ".tbz2") {
        args = {"-xjf", archive.string(), "-C", dest.string()};
    } else if (ext == ".xz" || ext == ".txz") {
        args = {"-xJf", archive.string(), "-C", dest.string()};
    } else if (ext == ".zip") {
        args = {"-q", archive.string(), "-d", dest.string()};
    } else {
        // Try tar with auto-detect
        args = {"-xf", archive.string(), "-C", dest.string()};
    }
    
    auto result = util::run("tar", args);
    if (result.exit_code != 0) {
        throw std::runtime_error("Failed to extract tarball: " + result.stderr_output);
    }
}

} // anonymous namespace

std::expected<void, InstallError> install_prebuilt(
    const LockEntry& entry,
    const std::filesystem::path& store_path
) {
    util::TempDir work;
    
    // Download the tarball
    std::filesystem::path archive = work.path() / "source.tarball";
    
    std::cout << "Fetching " << entry.name << " " << entry.version << "...\n";
    
    // Use curl to download
    auto result = util::run("curl", {"-L", "-o", archive.string(), entry.url});
    if (result.exit_code != 0) {
        std::cerr << "Download failed: " << result.stderr_output << "\n";
        return std::unexpected(InstallError::NetworkError);
    }
    
    // Verify checksum
    auto [algo, expected] = split_checksum(entry.checksum);
    std::string actual = compute_checksum(algo, archive);
    
    if (actual != expected) {
        std::cerr << "Checksum mismatch! Expected: " << expected << ", Got: " << actual << "\n";
        return std::unexpected(InstallError::ChecksumMismatch);
    }
    
    // Create store path
    std::filesystem::create_directories(store_path);
    
    // Extract
    std::cout << "Extracting...\n";
    try {
        extract_tarball(archive, store_path);
    } catch (const std::exception& e) {
        std::cerr << "Extraction failed: " << e.what() << "\n";
        std::filesystem::remove_all(store_path);
        return std::unexpected(InstallError::ExtractionFailed);
    }
    
    // Mark complete
    mark_complete(store_path);
    
    std::cout << "Installed " << entry.name << " " << entry.version << " to " << store_path << "\n";
    
    return {};
}

std::expected<void, InstallError> install_source(
    const LockEntry& entry,
    const std::filesystem::path& store_path,
    int max_jobs
) {
    util::TempDir work;
    
    // Download source
    std::filesystem::path archive = work.path() / "source.tarball";
    
    std::cout << "Fetching " << entry.name << " " << entry.version << " (source build)...\n";
    
    auto result = util::run("curl", {"-L", "-o", archive.string(), entry.url});
    if (result.exit_code != 0) {
        return std::unexpected(InstallError::NetworkError);
    }
    
    // Verify checksum
    auto [algo, expected] = split_checksum(entry.checksum);
    std::string actual = compute_checksum(algo, archive);
    
    if (actual != expected) {
        return std::unexpected(InstallError::ChecksumMismatch);
    }
    
    // Create store path
    std::filesystem::create_directories(store_path);
    
    // Extract
    try {
        extract_tarball(archive, work.path());
    } catch (...) {
        return std::unexpected(InstallError::ExtractionFailed);
    }
    
    // Find extracted directory
    std::filesystem::path source_dir;
    for (const auto& dir : std::filesystem::directory_iterator(work.path())) {
        if (dir.is_directory()) {
            source_dir = dir.path();
            break;
        }
    }
    
    if (source_dir.empty()) {
        return std::unexpected(InstallError::ExtractionFailed);
    }
    
    // Build using custom script or default autotools
    std::cout << "Building " << entry.name << "...\n";
    
    // Set ENVDIR environment variable for the build
    std::vector<std::pair<std::string, std::string>> env = {
        {"ENVDIR", store_path.string()}
    };
    
    if (entry.build_script && !entry.build_script->empty()) {
        // Use custom build script from the /bin repo entry
        std::string script = *entry.build_script;
        // Replace $ENVDIR placeholder
        size_t pos = 0;
        while ((pos = script.find("$ENVDIR", pos)) != std::string::npos) {
            script.replace(pos, 7, store_path.string());
            pos += store_path.string().length();
        }
        // Execute the build script via /bin/sh
        result = util::run("sh", {"-c", script}, source_dir, env);
        if (result.exit_code != 0) {
            std::cerr << "Build failed: " << result.stderr_output << "\n";
            return std::unexpected(InstallError::BuildFailed);
        }
    } else {
        // Default: try autotools
        // Configure
        result = util::run("./configure", {"--prefix=" + store_path.string()}, source_dir, env);
        if (result.exit_code != 0) {
            std::cerr << "Configure failed (no configure script?): " << result.stderr_output << "\n";
            // Try cmake as fallback
            result = util::run("cmake", {"-B", "build", "-DCMAKE_INSTALL_PREFIX=" + store_path.string()}, source_dir, env);
            if (result.exit_code != 0) {
                std::cerr << "CMake also failed: " << result.stderr_output << "\n";
                return std::unexpected(InstallError::BuildFailed);
            }
            result = util::run("cmake", {"--build", "build", "--", "-j" + std::to_string(max_jobs)}, source_dir, env);
            if (result.exit_code != 0) {
                std::cerr << "Build failed: " << result.stderr_output << "\n";
                return std::unexpected(InstallError::BuildFailed);
            }
            result = util::run("cmake", {"--install", "build"}, source_dir, env);
            if (result.exit_code != 0) {
                std::cerr << "Install failed: " << result.stderr_output << "\n";
                return std::unexpected(InstallError::BuildFailed);
            }
        } else {
            // Build
            std::string jobs = std::to_string(max_jobs);
            result = util::run("make", {"-j" + jobs}, source_dir, env);
            if (result.exit_code != 0) {
                std::cerr << "Build failed: " << result.stderr_output << "\n";
                return std::unexpected(InstallError::BuildFailed);
            }
            
            // Install
            result = util::run("make", {"install"}, source_dir, env);
            if (result.exit_code != 0) {
                std::cerr << "Install failed: " << result.stderr_output << "\n";
                return std::unexpected(InstallError::BuildFailed);
            }
        }
    }
    
    // Mark complete
    mark_complete(store_path);
    
    std::cout << "Built and installed " << entry.name << " " << entry.version << "\n";
    
    return {};
}

std::expected<void, InstallError> install_system(
    const LockEntry& entry
) {
    // Install via "astral -S" instead of to the store
    std::cout << "Installing " << entry.name << " " << entry.version << " via astral...\n";
    
    auto result = util::run("astral", {"-y", "-S", entry.name});
    if (result.exit_code != 0) {
        std::cerr << "Failed to install via astral: " << result.stderr_output << "\n";
        return std::unexpected(InstallError::BuildFailed);
    }
    
    std::cout << "Installed " << entry.name << " via astral.\n";
    return {};
}

std::expected<void, InstallError> install(
    const LockEntry& entry,
    const std::filesystem::path& store_root,
    int max_jobs,
    bool force
) {
    // Handle system install (via astral -S)
    if (entry.build_kind == lock::BuildKind::System) {
        return install_system(entry);
    }
    
    auto path = store_path(store_root, entry.name, entry.version, entry.checksum);
    
    // Check if already installed (unless force is true)
    if (!force && entry_exists(path)) {
        std::cout << entry.name << " " << entry.version << " already installed, skipping\n";
        return {};
    }
    
    // If force and exists, remove it first
    if (force && entry_exists(path)) {
        std::cout << "Forcing reinstall of " << entry.name << "...\n";
        std::filesystem::remove_all(path);
    }
    
    // Create parent directory if needed
    std::filesystem::create_directories(store_root);
    
    if (entry.build_kind == lock::BuildKind::Source) {
        return install_source(entry, path, max_jobs);
    } else {
        return install_prebuilt(entry, path);
    }
}

std::vector<std::filesystem::path> list_store_entries(const std::filesystem::path& store_root) {
    std::vector<std::filesystem::path> entries;
    
    if (!std::filesystem::exists(store_root)) {
        return entries;
    }
    
    for (const auto& dir : std::filesystem::directory_iterator(store_root)) {
        if (dir.is_directory() && entry_exists(dir.path())) {
            entries.push_back(dir.path());
        }
    }
    
    return entries;
}

uint64_t store_size(const std::filesystem::path& store_root) {
    uint64_t total = 0;
    
    for (const auto& entry : list_store_entries(store_root)) {
        for (const auto& dir : std::filesystem::recursive_directory_iterator(entry)) {
            if (dir.is_regular_file()) {
                total += dir.file_size();
            }
        }
    }
    
    return total;
}

} // namespace store
