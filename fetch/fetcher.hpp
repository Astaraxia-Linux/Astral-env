#pragma once

#include <filesystem>
#include <string>

namespace fetch {

struct DownloadSpec {
    std::string url;
    std::string checksum; // format: "sha256:abc123..."
};

class ChecksumError : public std::runtime_error {
public:
    ChecksumError(std::string_view file, std::string_view expected, std::string_view actual);
    const std::string& expected() const noexcept { return expected_; }
    const std::string& actual() const noexcept { return actual_; }
private:
    std::string expected_, actual_;
};

class NetworkError : public std::runtime_error {
public:
    explicit NetworkError(std::string_view url, std::string_view reason);
};

// Download url to dest, verify checksum.
// Throws ChecksumError if verification fails (and removes dest).
// Throws NetworkError on download failure.
void download_and_verify(
    const DownloadSpec& spec,
    const std::filesystem::path& dest
);

// Simple download without verification
void download(
    const std::string& url,
    const std::filesystem::path& dest
);

// Verify checksum of an existing file
void verify_checksum(
    const std::filesystem::path& file,
    const std::string& checksum
);

} // namespace fetch
