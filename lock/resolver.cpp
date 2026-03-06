#include "lock/resolver.hpp"
#include "repo/bin_repo.hpp"
#include "util/parse.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace lock {

namespace {

// FIX: Use util::trim from parse.hpp instead of local copy
int compare_versions(const std::string& a, const std::string& b) {
    // Simple version comparison - split by dots and compare numerically
    std::istringstream iss_a(a);
    std::istringstream iss_b(b);
    std::string part_a, part_b;
    
    while (true) {
        bool has_a = static_cast<bool>(std::getline(iss_a, part_a, '.'));
        bool has_b = static_cast<bool>(std::getline(iss_b, part_b, '.'));
        
        if (!has_a && !has_b) return 0;
        if (!has_a) return -1;
        if (!has_b) return 1;
        
        // Compare as numbers
        try {
            int num_a = std::stoi(part_a);
            int num_b = std::stoi(part_b);
            if (num_a < num_b) return -1;
            if (num_a > num_b) return 1;
        } catch (...) {
            // If not numbers, compare as strings
            if (part_a < part_b) return -1;
            if (part_a > part_b) return 1;
        }
    }
}

bool satisfies_constraint(const std::string& version, const VersionConstraint& constraint) {
    if (constraint.op.empty()) {
        return true; // Any version
    }
    
    int cmp = compare_versions(version, constraint.version);
    
    if (constraint.op == "=") {
        return cmp == 0;
    } else if (constraint.op == ">=") {
        return cmp >= 0;
    } else if (constraint.op == "<=") {
        return cmp <= 0;
    } else if (constraint.op == ">") {
        return cmp > 0;
    } else if (constraint.op == "<") {
        return cmp < 0;
    }
    
    return true;
}

} // anonymous namespace

VersionConstraint parse_constraint(std::string_view raw) {
    VersionConstraint constraint;
    
    std::string s = util::trim(std::string(raw));
    
    // Find operator
    size_t op_pos = std::string::npos;
    std::string op;
    
    if ((op_pos = s.find(">=")) != std::string::npos) {
        op = ">=";
    } else if ((op_pos = s.find("<=")) != std::string::npos) {
        op = "<=";
    } else if ((op_pos = s.find('>')) != std::string::npos) {
        op = ">";
    } else if ((op_pos = s.find('<')) != std::string::npos) {
        op = "<";
    } else if ((op_pos = s.find('=')) != std::string::npos) {
        op = "=";
    }
    
    if (op_pos != std::string::npos) {
        constraint.package = util::trim(s.substr(0, op_pos));
        constraint.op = op;
        constraint.version = util::trim(s.substr(op_pos + op.length()));
    } else {
        // No operator - any version
        constraint.package = s;
        constraint.op = "";
        constraint.version = "";
    }
    
    return constraint;
}

std::optional<repo::BinEntry> resolve(
    const std::filesystem::path& repo_bin_dir,
    const VersionConstraint& constraint
) {
    // Check if this is an explicit binary request
    bool wants_binary = constraint.package.ends_with("-bin");
    
    // 1. Try exact name in bin/ repo
    auto entries = repo::find_entries(repo_bin_dir, constraint.package);
    
    if (!entries.empty()) {
        // Filter by constraint and find best match
        std::optional<repo::BinEntry> best;
        
        for (auto& entry : entries) {
            if (!satisfies_constraint(entry.version, constraint)) {
                continue;
            }
            
            if (!best) {
                best = entry;
            } else {
                // Prefer newer version
                if (compare_versions(entry.version, best->version) > 0) {
                    best = entry;
                }
            }
        }
        
        return best;
    }
    
    // 2. Explicit -bin not found → hard error, no silent fallback
    if (wants_binary) {
        throw std::runtime_error(
            "Package '" + constraint.package + "' not found in bin/ repo. "
            "No pre-built binary is available.");
    }
    
    // 3. Source name not in bin/ → return nullopt, generate() will create a system entry
    return std::nullopt;
}

} // namespace lock
