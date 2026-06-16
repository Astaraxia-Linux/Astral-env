#include "repo/bin_repo.hpp"
#include "util/file.hpp"
#include "util/parse.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace repo {

namespace {

std::vector<std::string> split_space(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string part;
    while (iss >> part) {
        result.push_back(part);
    }
    return result;
}

} // anonymous namespace

BinEntry parse_entry(const std::filesystem::path& stars_file) {
    BinEntry entry;
    
    if (!std::filesystem::exists(stars_file)) {
        throw std::runtime_error("File not found: " + stars_file.string());
    }
    
    std::string content = util::read_file(stars_file);
    std::istringstream iss(content);
    
    bool in_metadata = false;
    bool in_source = false;
    bool in_provides = false;
    bool in_build = false;
    
    std::string build_script;
    std::string line;
    while (std::getline(iss, line)) {
        line = util::trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Track block state
        if (line.find("$ENV.Metadata") != std::string::npos) {
            in_metadata = true;
            in_source = false;
            in_provides = false;
            in_build = false;
            continue;
        }
        if (line.find("$ENV.Source") != std::string::npos) {
            in_metadata = false;
            in_source = true;
            in_provides = false;
            in_build = false;
            continue;
        }
        if (line.find("$ENV.Provides") != std::string::npos) {
            in_metadata = false;
            in_source = false;
            in_provides = true;
            in_build = false;
            continue;
        }
        if (line.find("$ENV.Build") != std::string::npos) {
            in_metadata = false;
            in_source = false;
            in_provides = false;
            in_build = true;
            continue;
        }
        if (line == "};") {
            in_metadata = false;
            in_source = false;
            in_provides = false;
            in_build = false;
            continue;
        }
        
        // Parse key = value
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            // Collect build script lines
            if (in_build) {
                build_script += line + "\n";
            }
            continue;
        }
        
        std::string key = util::trim(line.substr(0, eq_pos));
        std::string value = util::extract_value(line);
        
        if (in_metadata) {
            if (key == "Name") {
                entry.name = value;
            } else if (key == "Version") {
                entry.version = value;
            } else if (key == "Category") {
                entry.category = value;
            } else if (key == "Description") {
                entry.description = value;
            } else if (key == "Homepage") {
                entry.homepage = value;
            }
        } else if (in_source) {
            if (key == "url") {
                entry.url = value;
            } else if (key == "checksum") {
                entry.checksum = value;
            }
        } else if (in_provides) {
            if (key == "bin") {
                entry.provides_bin = split_space(value);
            } else if (key == "lib") {
                entry.provides_lib = split_space(value);
            } else if (key == "inc") {
                entry.provides_inc = split_space(value);
            } else if (key == "pkg") {
                entry.provides_pkg = split_space(value);
            }
        }
    }
    
    // Set build script if present
    if (!build_script.empty()) {
        entry.build_script = build_script;
    }
    
    // Extract name from filename if not set
    if (entry.name.empty()) {
        entry.name = stars_file.stem().string();
        // Remove version suffix if present (e.g., python-3.11 -> python)
        auto dash_pos = entry.name.rfind('-');
        if (dash_pos != std::string::npos) {
            // Check if the suffix looks like a version
            auto suffix = entry.name.substr(dash_pos + 1);
            if (suffix.find('.') != std::string::npos) {
                entry.name = entry.name.substr(0, dash_pos);
            }
        }
    }
    
    return entry;
}

std::vector<BinEntry> find_entries(
    const std::filesystem::path& repo_bin_dir,
    std::string_view package_name
) {
    std::vector<BinEntry> entries;
    
    if (!std::filesystem::exists(repo_bin_dir)) {
        return entries;
    }
    
    // Look for exact match and versioned matches
    std::string base_name(package_name);
    
    for (const auto& dir : std::filesystem::recursive_directory_iterator(repo_bin_dir)) {
        if (!dir.is_regular_file()) continue;
        
        auto path = dir.path();
        if (path.extension() != ".stars") continue;
        
        auto stem = path.stem().string();
        
        // Match exact name or name-version pattern
        if (stem == base_name || 
            (stem.find(base_name + "-") == 0)) {
            try {
                entries.push_back(parse_entry(path));
            } catch (...) {
                // Skip invalid entries
            }
        }
    }
    
    return entries;
}

std::optional<BinEntry> find_latest(
    const std::filesystem::path& repo_bin_dir,
    std::string_view package_name
) {
    auto entries = find_entries(repo_bin_dir, package_name);
    
    if (entries.empty()) {
        return std::nullopt;
    }
    
    // Return the first one (they should be sorted by version in a real implementation)
    return entries.front();
}

std::vector<std::string> list_categories(const std::filesystem::path& repo_bin_dir) {
    std::vector<std::string> categories;
    
    if (!std::filesystem::exists(repo_bin_dir)) {
        return categories;
    }
    
    for (const auto& dir : std::filesystem::directory_iterator(repo_bin_dir)) {
        if (dir.is_directory()) {
            categories.push_back(dir.path().filename().string());
        }
    }
    
    std::sort(categories.begin(), categories.end());
    return categories;
}

std::vector<std::string> list_packages(const std::filesystem::path& repo_bin_dir, const std::string& category) {
    std::vector<std::string> packages;
    
    auto category_dir = repo_bin_dir / category;
    if (!std::filesystem::exists(category_dir)) {
        return packages;
    }
    
    for (const auto& dir : std::filesystem::directory_iterator(category_dir)) {
        if (dir.is_regular_file() && dir.path().extension() == ".stars") {
            packages.push_back(dir.path().stem().string());
        }
    }
    
    std::sort(packages.begin(), packages.end());
    return packages;
}

} // namespace repo
