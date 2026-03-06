#include "util/file.hpp"
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace util {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void write_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }
    file << content;
}

void append_file(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for appending: " + path.string());
    }
    file << content;
}

bool file_exists(const std::filesystem::path& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

} // namespace util
