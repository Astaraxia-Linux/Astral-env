#pragma once

#include "env/node.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace astral_sys {

struct I18nConfig {
    std::string              default_locale;
    std::map<std::string, std::string> extra;
};

struct ConsoleConfig {
    std::string font;
    std::string keymap;
};

struct RepoConfig {
    std::string              name;
    std::string              url;
    std::vector<std::string> mirrors;
    int                      priority_order = 99;
    bool                     is_fallback    = false;
};

struct FontConfig {
    std::vector<std::string> nerd_fonts;
};

struct PackagesConfig {
    bool                     binary_pkg = false;
    std::vector<RepoConfig>  repos;
    FontConfig               fonts;
    std::vector<std::string> system_packages;
};

struct KernelConfig {
    std::vector<std::string> initrd_modules;
    std::vector<std::string> modules;
    std::vector<std::string> params;
    std::string              loader_type;
    int                      loader_timeout = 0;
};

struct HardwareConfig {
    std::string cpu;          // "amd" | "intel" — enables microcode
    bool        all_firmware  = false;
    std::string graphics;     // "amdgpu" | "nvidia" | "i915" etc.
};

struct DiskEntry {
    std::string path;         // mount point
    std::string disk_path;    // device path or "none"
    std::string fs_type;
};

struct NetworkInterface {
    std::string name;
    bool        use_dhcp = true;
    // Static IP (only used when use_dhcp = false)
    std::string              address;   // CIDR, e.g. "192.168.1.100/24"
    std::string              gateway;
    std::vector<std::string> dns;
};

struct NetworkConfig {
    bool                          use_dhcp = true;
    std::vector<NetworkInterface> interfaces;
};

struct UserEntry {
    std::string              name;
    bool                     normal_user = true;
    std::string              home_dir;
    std::vector<std::string> groups;
    std::string              config_path;
};

struct SystemConfig {
    std::string              hostname;
    std::string              timezone;
    std::string              layout;
    std::string              xkb_variant;
    I18nConfig               i18n;
    ConsoleConfig            console;
    bool                     xserver = false;
    PackagesConfig           packages;
    std::vector<std::pair<std::string, std::string>> services; // name -> state

    KernelConfig             kernel;
    HardwareConfig           hardware;
    std::vector<DiskEntry>   filesystems;
    NetworkConfig            network;

    std::vector<UserEntry>   users;

    // Raw tree — stores everything not explicitly mapped above
    env::NodeMap             raw;
};

struct UserConfig {
    std::string              username;
    std::string              shell;
    std::vector<std::string> groups;
    std::vector<std::string> packages;
    std::map<std::string, std::string> aliases;
    std::vector<std::pair<std::string, std::string>> dotfiles; // dest -> src
    std::vector<std::pair<std::string, std::string>> symlinks; // dest -> src
    std::map<std::string, std::string> vars;

    env::NodeMap raw;
};

SystemConfig parse_system_config(const std::filesystem::path& path);
UserConfig   parse_user_config(const std::filesystem::path& path);

std::vector<std::pair<std::string, UserConfig>>
load_all_user_configs(const std::filesystem::path& config_dir);

} // namespace astral_sys
