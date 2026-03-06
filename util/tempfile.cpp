#include "util/tempfile.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace util {

TempFile::TempFile(std::string_view suffix) {
    auto tmp = std::filesystem::temp_directory_path();
    auto tmpl = tmp / (std::string("astral-env-XXXXXX") + std::string(suffix));
    std::string s = tmpl.string();
    
    int fd = mkstemp(s.data());
    if (fd == -1) {
        throw std::runtime_error("Failed to create temp file");
    }
    close(fd);
    
    path_ = s;
}

TempFile::~TempFile() {
    if (!released_ && !path_.empty()) {
        std::filesystem::remove(path_);
    }
}

TempFile::TempFile(TempFile&& other) noexcept
    : path_(std::move(other.path_)), released_(other.released_) {
    other.released_ = true;
}

TempFile& TempFile::operator=(TempFile&& other) noexcept {
    if (this != &other) {
        if (!released_ && !path_.empty()) {
            std::filesystem::remove(path_);
        }
        path_ = std::move(other.path_);
        released_ = other.released_;
        other.released_ = true;
    }
    return *this;
}

} // namespace util
