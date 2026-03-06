#pragma once

#include <filesystem>
#include <string>

namespace util {

class TempDir {
public:
    TempDir();
    explicit TempDir(std::string_view prefix);
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) noexcept;
    TempDir& operator=(TempDir&&) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void release() noexcept { released_ = true; }

private:
    std::filesystem::path path_;
    bool released_ = false;
};

} // namespace util
