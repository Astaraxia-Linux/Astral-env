#include "repo/bin_repo.hpp"
#include "util/file.hpp"
#include "util/parse.hpp"
#include <algorithm>
#include <cctype>
#include <set>
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

    // Layout: bin/<arch>/<cat>/ — iterate arch dirs, collect unique category names
    std::set<std::string> seen;
    for (const auto& arch_dir : std::filesystem::directory_iterator(repo_bin_dir)) {
        if (!arch_dir.is_directory()) continue;
        for (const auto& cat_dir : std::filesystem::directory_iterator(arch_dir.path())) {
            if (!cat_dir.is_directory()) continue;
            auto cat = cat_dir.path().filename().string();
            if (seen.insert(cat).second)
                categories.push_back(cat);
        }
    }

    std::sort(categories.begin(), categories.end());
    return categories;
}

std::vector<std::string> list_packages(const std::filesystem::path& repo_bin_dir, const std::string& category) {
    std::vector<std::string> result;
    auto cat_dir = repo_bin_dir / category;
    if (!std::filesystem::exists(cat_dir)) return result;

    // Layout: bin/<arch>/<cat>/<shard>/<pkg>-<ver>.stars
    for (const auto& shard_entry : std::filesystem::directory_iterator(cat_dir)) {
        if (!shard_entry.is_directory()) continue;
        for (const auto& entry : std::filesystem::directory_iterator(shard_entry.path())) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().string();
            if (name.size() > 6 && name.substr(name.size() - 6) == ".stars")
                result.push_back(name.substr(0, name.size() - 6));
        }
    }
    return result;
}

} // namespace repo
