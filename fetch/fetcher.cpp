#include "fetch/fetcher.hpp"
#include "util/process.hpp"
#include <stdexcept>
#include <sstream>
#include <regex>

namespace fetch {

ChecksumError::ChecksumError(std::string_view file, std::string_view expected, std::string_view actual)
    : std::runtime_error("Checksum mismatch"), expected_(expected), actual_(actual) {}

NetworkError::NetworkError(std::string_view url, std::string_view reason)
    : std::runtime_error("Network error fetching " + std::string(url) + ": " + std::string(reason)) {}

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

} // anonymous namespace

void download(
    const std::string& url,
    const std::filesystem::path& dest
) {
    // Use curl to download
    auto result = util::run("curl", {"-L", "-f", "-o", dest.string(), url});
    
    if (result.exit_code != 0) {
        throw NetworkError(url, result.stderr_output.empty() ? "curl failed" : result.stderr_output);
    }
}

void verify_checksum(
    const std::filesystem::path& file,
    const std::string& checksum
) {
    auto [algo, expected] = split_checksum(checksum);
    std::string actual = compute_checksum(algo, file);
    
    if (actual != expected) {
        throw ChecksumError(file.string(), expected, actual);
    }
}

void download_and_verify(
    const DownloadSpec& spec,
    const std::filesystem::path& dest
) {
    // RAII cleanup guard
    struct Cleanup {
        const std::filesystem::path& path;
        bool committed = false;
        ~Cleanup() { 
            if (!committed && std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    } cleanup{dest};
    
    // Download
    download(spec.url, dest);
    
    // Verify
    verify_checksum(dest, spec.checksum);
    
    cleanup.committed = true; // keep the file
}

} // namespace fetch
