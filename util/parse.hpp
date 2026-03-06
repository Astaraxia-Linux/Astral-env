#pragma once

#include <string>
#include <algorithm>
#include <cctype>
#include <utility>

namespace util {

// Trim whitespace from both ends of a string
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract value from key=value line
inline std::string extract_value(const std::string& line) {
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) return "";
    std::string value = line.substr(eq_pos + 1);
    value = trim(value);
    // Remove quotes
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

// Split checksum string into algorithm and hash
inline std::pair<std::string, std::string> split_checksum(const std::string& s) {
    auto colon_pos = s.find(':');
    if (colon_pos == std::string::npos) {
        return {"sha256", s};
    }
    return {s.substr(0, colon_pos), s.substr(colon_pos + 1)};
}

} // namespace util
