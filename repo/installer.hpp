#pragma once

#include "repo/registry.hpp"
#include "repo/index.hpp"
#include "system/system_conf.hpp"
#include <string>
#include <vector>

namespace repo {

enum class InstallKind { Source, Binary, Font, System };

struct InstallResult {
    std::string pkg;
    bool        ok  = false;
    std::string err;
};

// Install a single package.
// Resolution order:
//   1. /bin/<cat>/<pkg>-<ver>-<arch>.tar.* in any repo  → binary install to store
//   2. /recipes/<cat>/.../<pkg>-<ver>-<arch>.stars      → delegate to astral -S
//   3. not found                                         → error
InstallResult install_pkg(const std::string& pkg_name,
                           const std::vector<RepoEntry>& repos,
                           bool binary_pkg,
                           const std::filesystem::path& cache_dir = "/var/lib/astral-env/index",
                           const std::filesystem::path& store_root = "/astral-env/store");

// Install a NerdFont family from /font/<family>/<font>.tar.*
InstallResult install_font(const std::string& font_name,
                            const std::vector<RepoEntry>& repos,
                            const std::filesystem::path& font_dest = "/usr/share/fonts");

// Install all packages and fonts declared in a SystemConfig.
// Returns number of failures.
int install_all(const astral_sys::SystemConfig& cfg,
                const std::vector<RepoEntry>& repos,
                const std::filesystem::path& cache_dir = "/var/lib/astral-env/index",
                const std::filesystem::path& store_root = "/astral-env/store",
                const std::filesystem::path& font_dest  = "/usr/share/fonts");

// Install packages for a specific user config.
int install_user_pkgs(const astral_sys::UserConfig& ucfg,
                      const std::vector<RepoEntry>& repos,
                      const std::filesystem::path& user_store);

} // namespace repo
