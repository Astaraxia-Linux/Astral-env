#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace util {

class TempFile {
public:
    explicit TempFile(std::string_view suffix = "");
    ~TempFile();

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) noexcept;
    TempFile& operator=(TempFile&&) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void release() noexcept { released_ = true; }

private:
    std::filesystem::path path_;
    bool released_ = false;
};

} // namespace util
