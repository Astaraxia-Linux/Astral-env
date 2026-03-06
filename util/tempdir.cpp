#include "util/tempdir.hpp"
#include <stdexcept>
#include <cstring>

namespace util {

TempDir::TempDir(std::string_view prefix) {
    auto tmp = std::filesystem::temp_directory_path();
    auto tmpl = tmp / (std::string(prefix) + "XXXXXX");
    std::string s = tmpl.string();
    if (!mkdtemp(s.data())) {
        throw std::runtime_error("Failed to create temp directory");
    }
    path_ = s;
}

TempDir::TempDir() : TempDir("astral-env-") {}

TempDir::~TempDir() {
    if (!released_ && !path_.empty()) {
        std::filesystem::remove_all(path_);
    }
}

TempDir::TempDir(TempDir&& other) noexcept
    : path_(std::move(other.path_)), released_(other.released_) {
    other.released_ = true;
}

TempDir& TempDir::operator=(TempDir&& other) noexcept {
    if (this != &other) {
        if (!released_ && !path_.empty()) {
            std::filesystem::remove_all(path_);
        }
        path_ = std::move(other.path_);
        released_ = other.released_;
        other.released_ = true;
    }
    return *this;
}

} // namespace util
