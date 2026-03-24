#pragma once

#include "env/node.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

namespace env {

class ParseError : public std::runtime_error {
public:
    ParseError(int line, std::string_view msg)
        : std::runtime_error("line " + std::to_string(line) + ": " + std::string(msg)) {}
};

// Parse a .stars ENV file.
// Handles $ENV: { ... } root, dot-shorthand keys, Includes merging.
// base_dir is used to resolve relative Includes paths.
NodeMap parse_file(const std::filesystem::path& path);

// Parse from string (for testing / inline config)
NodeMap parse_str(std::string_view src, std::string_view name = "<string>",
                  const std::filesystem::path& base_dir = ".");

} // namespace env
