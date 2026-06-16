#include "store/store.hpp"
#include "util/file.hpp"
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <optional>
#include <chrono>

namespace store {

std::string compute_hash(
    std::string_view name,
    std::string_view version,
    std::string_view source_checksum
) {
    // Input: name-version-checksum
    std::string input = std::string(name) + "-" + std::string(version) + "-" + std::string(source_checksum);
    
    // Compute SHA256 via EVP (OpenSSL 3.0 compatible)
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_Digest(input.data(), input.size(), hash, &hash_len, EVP_sha256(), nullptr);

    // Convert full SHA-256 to hex string (32 bytes = 64 hex chars)
    std::ostringstream ss;
    ss << "sha256-";
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

std::filesystem::path store_path(
    const std::filesystem::path& store_root,
    std::string_view name,
    std::string_view version,
    std::string_view source_checksum
) {
    std::string hash = compute_hash(name, version, source_checksum);
    
    std::ostringstream ss;
    ss << hash << "-" << name << "-" << version;
    
    return store_root / ss.str();
}

bool entry_exists(const std::filesystem::path& entry_path) {
    auto marker = entry_path / ".complete";
    return std::filesystem::exists(marker);
}

void mark_complete(const std::filesystem::path& entry_path) {
    auto marker = entry_path / ".complete";
    // Write Unix seconds (not nanoseconds) to match what get_completion_time expects
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f(marker);
    f << secs;
}

std::optional<std::chrono::system_clock::time_point> get_completion_time(
    const std::filesystem::path& entry_path
) {
    auto marker = entry_path / ".complete";
    if (!std::filesystem::exists(marker)) {
        return std::nullopt;
    }
    
    try {
        std::string content = util::read_file(marker);
        auto timestamp = std::stoll(content);
        return std::chrono::system_clock::time_point(std::chrono::seconds(timestamp));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace store