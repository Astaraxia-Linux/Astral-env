#include "repo/index.hpp"
#include "util/process.hpp"
#include "util/file.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace repo {

namespace {

// ─── helpers ────────────────────────────────────────────────────────────────

std::string trim_str(std::string_view sv) {
    auto a = sv.find_first_not_of(" \t");
    auto b = sv.find_last_not_of(" \t");
    return (a == std::string_view::npos) ? std::string{} : std::string(sv.substr(a, b - a + 1));
}

// Detect which format a line introduces.
// v2.1: first non-comment, non-empty line starts with "/" or a word followed by ":"
// Legacy: first data line contains "|"
enum class IndexFormat { Unknown, Legacy, V21 };

IndexFormat detect_format(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        auto s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        if (line.empty() || line[0] == '#') continue;
        // v2.1 opens with "/recipes:", "/bin:", "/font:", or a bare word section
        if (line[0] == '/') return IndexFormat::V21;
        // Legacy: contains pipe separator or is a category header like "app-arch: {"
        if (line.find('|') != std::string::npos) return IndexFormat::Legacy;
        // Could be v2.1 section header (word: {) or legacy category
        return IndexFormat::V21;
    }
    return IndexFormat::Unknown;
}

// ─── legacy parser (old format) ──────────────────────────────────────────────
// Format:
//   app-misc: {
//       neofetch | any | 7.1.0
//   }

std::vector<PkgEntry> parse_legacy(const std::string& content, const std::string& repo_name) {
    std::vector<PkgEntry> out;
    std::istringstream iss(content);
    std::string current_cat;
    std::string line;

    while (std::getline(iss, line)) {
        auto s = line.find_first_not_of(" \t");
        auto e = line.find_last_not_of(" \t");
        if (s == std::string::npos) continue;
        line = line.substr(s, e - s + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line == "}" || line == "};") continue;

        // Category header: "name:" or "name: {" etc.
        if (line.back() == '{' || line.back() == ':' ||
            (line.size() > 2 && line[line.size()-2] == ':')) {
            current_cat = trim_str(line.substr(0, line.find(':')));
            continue;
        }

        // Package line: pkg | arch | version
        auto f1 = line.find('|');
        if (f1 == std::string::npos) continue;
        auto f2 = line.find('|', f1 + 1);
        if (f2 == std::string::npos) continue;

        PkgEntry p;
        p.cat_pkg   = current_cat + "/" + trim_str(line.substr(0, f1));
        p.arch      = trim_str(line.substr(f1 + 1, f2 - f1 - 1));
        p.version   = trim_str(line.substr(f2 + 1));
        p.repo_name = repo_name;
        out.push_back(std::move(p));
    }
    return out;
}

// ─── v2.1 parser ─────────────────────────────────────────────────────────────
// Format:
//   /recipes: {
//     app-arch: {
//       bzip2: {
//         1.0.8
//         git
//       }
//     }
//   }
//   /bin: {
//     x86_64: {
//       app-arch: {
//         bzip2: {
//           1.0.8
//         }
//       }
//     }
//   }
//   /font: { ... }   (informational only — produces no PkgEntry)

std::vector<PkgEntry> parse_v21(const std::string& content, const std::string& repo_name) {
    std::vector<PkgEntry> out;
    std::istringstream iss(content);
    std::string line;

    // State machine: depth-tracking stack
    // section: "recipes" | "bin" | "font" | ""
    std::string section;
    std::string arch;       // only in /bin
    std::string category;
    std::string pkg_name;
    int depth = 0;          // brace depth relative to section open

    while (std::getline(iss, line)) {
        auto s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        if (line.empty() || line[0] == '#') continue;

        // Strip trailing comment
        auto comment = line.find(" #");
        if (comment != std::string::npos) line = trim_str(line.substr(0, comment));

        // Closing brace
        if (line == "}" || line == "};") {
            if (depth > 0) --depth;
            if (depth == 0) {
                // Left section
                section.clear(); arch.clear(); category.clear(); pkg_name.clear();
            } else if (depth == 1 && section == "bin") {
                // Left arch block
                arch.clear(); category.clear(); pkg_name.clear();
            } else if ((depth == 1 && section == "recipes") ||
                       (depth == 2 && section == "bin")) {
                category.clear(); pkg_name.clear();
            } else if ((depth == 2 && section == "recipes") ||
                       (depth == 3 && section == "bin")) {
                pkg_name.clear();
            }
            continue;
        }

        // Section header: "/recipes:", "/bin:", "/font:"
        if (line[0] == '/') {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                section = trim_str(line.substr(1, colon - 1));
                depth = 1;
                arch.clear(); category.clear(); pkg_name.clear();
            }
            continue;
        }

        // Opening block: "word: {" or "word {"
        auto open = line.find('{');
        if (open != std::string::npos) {
            std::string key = trim_str(line.substr(0, line.find_first_of(": {")));
            ++depth;
            if (section == "recipes") {
                if (depth == 2)      category = key;
                else if (depth == 3) pkg_name  = key;
            } else if (section == "bin") {
                if (depth == 2)      arch      = key;
                else if (depth == 3) category  = key;
                else if (depth == 4) pkg_name  = key;
            }
            // font: informational, no PkgEntry generation
            continue;
        }

        // Bare value: a version string or plain name
        if (section == "recipes" && depth == 3 && !category.empty() && !pkg_name.empty()) {
            // line is a version (e.g. "1.0.8" or "git")
            PkgEntry p;
            p.cat_pkg   = category + "/" + pkg_name;
            p.arch      = "any";
            p.version   = line;
            p.repo_name = repo_name;
            out.push_back(std::move(p));
        } else if (section == "bin" && depth == 4 && !arch.empty() && !category.empty() && !pkg_name.empty()) {
            PkgEntry p;
            p.cat_pkg   = category + "/" + pkg_name;
            p.arch      = arch;
            p.version   = line;
            p.repo_name = repo_name;
            out.push_back(std::move(p));
        }
    }
    return out;
}

} // anonymous namespace

// ─── public API ──────────────────────────────────────────────────────────────

std::vector<PkgEntry> parse_index(const std::filesystem::path& index_file,
                                   const std::string& repo_name) {
    if (!std::filesystem::exists(index_file)) return {};

    std::string content = util::read_file(index_file);

    switch (detect_format(content)) {
        case IndexFormat::V21:    return parse_v21(content,    repo_name);
        case IndexFormat::Legacy: return parse_legacy(content, repo_name);
        default: return {};
    }
}

void sync_indexes(const std::vector<RepoEntry>& repos,
                  const std::filesystem::path& cache_dir) {
    std::filesystem::create_directories(cache_dir);
    for (const auto& repo : repos) {
        auto dest = cache_dir / (repo.name + ".index");
        auto url  = index_url(repo);
        std::cout << "Syncing index: " << repo.name << " (" << url << ")\n";
        auto r = util::run("curl", {
            "-fsSL",
            "-A", "Mozilla/5.0 (X11; Linux x86_64; rv:124.0) Gecko/20100101 Firefox/124.0",
            url, "-o", dest.string()
        });
        if (r.exit_code != 0)
            std::cerr << "  warning: failed to fetch index for " << repo.name
                      << ": " << r.stderr_output << "\n";
        else
            std::cout << "  OK — " << dest << "\n";
    }
}

std::vector<PkgEntry> query(const std::string& pkg_name,
                             const std::vector<RepoEntry>& repos,
                             const std::filesystem::path& cache_dir) {
    std::vector<PkgEntry> results;
    // Extract bare name (no category prefix)
    std::string bare = pkg_name;
    auto slash = bare.rfind('/');
    if (slash != std::string::npos) bare = bare.substr(slash + 1);

    for (const auto& repo : repos) {
        auto idx = cache_dir / (repo.name + ".index");
        auto entries = parse_index(idx, repo.name);
        for (auto& e : entries) {
            auto p = e.cat_pkg.rfind('/');
            std::string name = (p == std::string::npos) ? e.cat_pkg : e.cat_pkg.substr(p + 1);
            if (name == bare || e.cat_pkg == pkg_name)
                results.push_back(e);
        }
    }
    return results;
}

} // namespace repo
