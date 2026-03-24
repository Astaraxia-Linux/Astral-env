#include "repo/installer.hpp"
#include "repo/index.hpp"
#include "repo/registry.hpp"
#include "store/store.hpp"
#include "store/entry.hpp"
#include "util/process.hpp"
#include "util/tempdir.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

namespace repo {

namespace {

// Download and extract a tarball to dest_dir
bool fetch_and_extract(const std::string& url, const std::filesystem::path& dest_dir) {
    util::TempDir work;
    auto archive = work.path() / "pkg.tar";

    auto dl = util::run("curl", {"-fsSL", url, "-o", archive.string()});
    if (dl.exit_code != 0) return false;

    std::filesystem::create_directories(dest_dir);
    auto ex = util::run("tar", {"-xf", archive.string(), "-C", dest_dir.string()});
    return ex.exit_code == 0;
}

// Try to find and install a binary package from /bin/
std::optional<InstallResult> try_binary(const std::string& pkg_name,
                                         const PkgEntry& entry,
                                         const RepoEntry& repo,
                                         const std::filesystem::path& store_root) {
    // Filename convention: <pkg>-<ver>-<arch>.tar.zst (or .tar.gz, .tar.xz)
    std::vector<std::string> exts = {".tar.zst", ".tar.xz", ".tar.gz", ".tar.bz2"};

    auto slug = entry.cat_pkg.substr(entry.cat_pkg.rfind('/') + 1);
    auto cat  = entry.cat_pkg.substr(0, entry.cat_pkg.rfind('/'));

    for (const auto& ext : exts) {
        std::string filename = slug + "-" + entry.version + "-" + entry.arch + ext;
        std::string url = bin_url(repo, cat, filename);

        auto store_path = store_root / (slug + "-" + entry.version);
        if (std::filesystem::exists(store_path / ".complete")) {
            return InstallResult{pkg_name, true, ""};
        }

        if (fetch_and_extract(url, store_path)) {
            // Mark complete
            store::mark_complete(store_path);
            // Symlink binaries into /usr/local/bin
            auto bin_dir = store_path / "usr" / "bin";
            if (!std::filesystem::exists(bin_dir))
                bin_dir = store_path / "bin";
            if (std::filesystem::exists(bin_dir)) {
                std::filesystem::create_directories("/usr/local/bin");
                for (const auto& f : std::filesystem::directory_iterator(bin_dir)) {
                    if (!f.is_regular_file()) continue;
                    auto link = std::filesystem::path("/usr/local/bin") / f.path().filename();
                    std::error_code ec;
                    if (std::filesystem::is_symlink(link)) std::filesystem::remove(link, ec);
                    std::filesystem::create_symlink(f.path(), link, ec);
                }
            }
            std::cout << "  [bin] installed " << pkg_name << " " << entry.version << "\n";
            return InstallResult{pkg_name, true, ""};
        }
    }
    return std::nullopt;
}

// Delegate to astral for source builds
InstallResult try_source(const std::string& pkg_name) {
    std::cout << "  [src] delegating to astral: " << pkg_name << "\n";
    auto r = util::run("astral", {"-y", "-S", pkg_name});
    if (r.exit_code == 0)
        return {pkg_name, true, ""};
    return {pkg_name, false, r.stderr_output};
}

} // anonymous namespace

InstallResult install_pkg(const std::string& pkg_name,
                           const std::vector<RepoEntry>& repos,
                           bool binary_pkg,
                           const std::filesystem::path& cache_dir,
                           const std::filesystem::path& store_root) {
    auto entries = query(pkg_name, repos, cache_dir);
    if (entries.empty()) {
        // Not in index — try astral directly (handles locally cached recipes etc.)
        return try_source(pkg_name);
    }

    // Pick best entry: prefer "any" arch, then host arch
    const PkgEntry* best = &entries[0];
    for (const auto& e : entries) {
        if (e.arch == "any") { best = &e; break; }
    }

    // Find which repo this entry belongs to
    const RepoEntry* repo_ptr = nullptr;
    for (const auto& r : repos) {
        if (r.name == best->repo_name) { repo_ptr = &r; break; }
    }

    if (binary_pkg && repo_ptr) {
        if (auto res = try_binary(pkg_name, *best, *repo_ptr, store_root))
            return *res;
        // Binary not found — fall through to source
    }

    return try_source(pkg_name);
}

InstallResult install_font(const std::string& font_name,
                            const std::vector<RepoEntry>& repos,
                            const std::filesystem::path& font_dest) {
    std::vector<std::string> exts = {".tar.zst", ".tar.xz", ".tar.gz"};
    for (const auto& repo : repos) {
        for (const auto& ext : exts) {
            // Try <font_name>/<font_name>-nerd.tar.* and <font_name>/<font_name>.tar.*
            for (const auto& suffix : std::vector<std::string>{"-nerd", ""}) {
                std::string filename = font_name + suffix + ext;
                std::string url = font_url(repo, font_name, filename);
                auto dest = font_dest / "truetype" / font_name;
                if (fetch_and_extract(url, dest)) {
                    // Rebuild font cache
                    util::run("fc-cache", {"-fv", dest.string()});
                    std::cout << "  [font] installed " << font_name << "\n";
                    return {font_name, true, ""};
                }
            }
        }
    }
    return {font_name, false, "not found in any repo"};
}

int install_all(const astral_sys::SystemConfig& cfg,
                const std::vector<RepoEntry>& repos,
                const std::filesystem::path& cache_dir,
                const std::filesystem::path& store_root,
                const std::filesystem::path& font_dest) {
    int failures = 0;

    for (const auto& pkg : cfg.packages.system_packages) {
        auto res = install_pkg(pkg, repos, cfg.packages.binary_pkg, cache_dir, store_root);
        if (!res.ok) {
            std::cerr << "  FAIL: " << pkg << ": " << res.err << "\n";
            ++failures;
        }
    }

    for (const auto& font : cfg.packages.fonts.nerd_fonts) {
        auto res = install_font(font, repos, font_dest);
        if (!res.ok) {
            std::cerr << "  FAIL font: " << font << ": " << res.err << "\n";
            ++failures;
        }
    }

    return failures;
}

int install_user_pkgs(const astral_sys::UserConfig& ucfg,
                      const std::vector<RepoEntry>& repos,
                      const std::filesystem::path& user_store) {
    int failures = 0;
    std::filesystem::create_directories(user_store);

    for (const auto& pkg : ucfg.packages) {
        // User packages go to user_store, binaries symlinked under user_store/bin
        auto res = install_pkg(pkg, repos, false,
                               "/var/lib/astral-env/index", user_store);
        if (!res.ok) {
            std::cerr << "  FAIL user pkg: " << pkg << ": " << res.err << "\n";
            ++failures;
        }
    }
    return failures;
}

} // namespace repo
