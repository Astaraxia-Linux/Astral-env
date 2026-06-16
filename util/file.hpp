#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace util {

// RAII wrapper for FILE*
class File {
public:
    File() = default;
    explicit File(FILE* file) : file_(file) {}
    ~File() { close(); }
    
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    File(File&& other) noexcept : file_(other.file_) {
        other.file_ = nullptr;
    }
    File& operator=(File&& other) noexcept {
        if (this != &other) {
            close();
            file_ = other.file_;
            other.file_ = nullptr;
        }
        return *this;
    }
    
    void close() {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }
    
    [[nodiscard]] FILE* get() const noexcept { return file_; }
    explicit operator bool() const noexcept { return file_ != nullptr; }
    
private:
    FILE* file_ = nullptr;
};

// Read entire file into string
std::string read_file(const std::filesystem::path& path);

// Write string to file
void write_file(const std::filesystem::path& path, std::string_view content);

// Append string to file
void append_file(const std::filesystem::path& path, std::string_view content);

// Check if file exists
bool file_exists(const std::filesystem::path& path);

} // namespace util
