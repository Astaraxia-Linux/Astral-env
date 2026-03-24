#include "system/differ.hpp"
#include "system/system_conf.hpp"
#include "util/process.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace astral_sys {

namespace {

// Check astral's own DB — works regardless of host package manager
bool is_package_installed(const std::string& pkg) {
    static const std::filesystem::path db("/var/lib/astral/db");
    if (!std::filesystem::exists(db)) return false;

    for (const auto& cat : std::filesystem::directory_iterator(db)) {
        if (!cat.is_directory()) continue;
        auto ver = cat.path() / pkg / "version";
        if (std::filesystem::exists(ver)) return true;
    }
    return false;
}

enum class InitSystem { Systemd, OpenRC, Runit, S6, Dinit, SysV, Unknown };

InitSystem detect_init() {
    if (std::filesystem::exists("/run/systemd/private"))       return InitSystem::Systemd;
    if (std::filesystem::exists("/run/openrc/softlevel"))      return InitSystem::OpenRC;
    if (std::filesystem::exists("/run/runit"))                 return InitSystem::Runit;
    if (std::filesystem::exists("/run/s6"))                    return InitSystem::S6;
    if (std::filesystem::exists("/run/dinit"))                 return InitSystem::Dinit;
    if (std::filesystem::exists("/etc/inittab"))               return InitSystem::SysV;
    return InitSystem::Unknown;
}

std::string get_service_state(const std::string& svc) {
    switch (detect_init()) {
        case InitSystem::Systemd: {
            auto r = util::run("systemctl", {"is-enabled", svc});
            if (r.exit_code == 0) return "enabled";
            return "disabled";
        }
        case InitSystem::OpenRC: {
            auto r = util::run("rc-update", {"show"});
            if (r.stdout_output.find(svc) != std::string::npos) return "enabled";
            return "disabled";
        }
        default:
            return "unknown";
    }
}

std::string get_hostname() {
    // Prefer /etc/hostname (portable, no tool dependency)
    std::ifstream f("/etc/hostname");
    if (f) {
        std::string h;
        std::getline(f, h);
        if (!h.empty()) return h;
    }
    auto r = util::run("hostname", {});
    auto& s = r.stdout_output;
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::string get_timezone() {
    // /etc/localtime is a symlink to the zone file under zoneinfo
    std::error_code ec;
    auto target = std::filesystem::read_symlink("/etc/localtime", ec);
    if (!ec) {
        std::string t = target.string();
        auto pos = t.find("zoneinfo/");
        if (pos != std::string::npos) return t.substr(pos + 9);
    }
    // Fallback: /etc/timezone (Debian-style)
    std::ifstream f("/etc/timezone");
    if (f) {
        std::string tz;
        std::getline(f, tz);
        if (!tz.empty()) return tz;
    }
    return "";
}

} // anonymous namespace

std::vector<DiffEntry> diff_system(
    const SystemConfig& declared,
    const std::vector<std::pair<std::string, UserConfig>>& user_configs
) {
    std::vector<DiffEntry> diffs;

    if (!declared.hostname.empty() && declared.hostname != get_hostname())
        diffs.push_back({"system", "hostname", DiffAction::Change, get_hostname(), declared.hostname});

    if (!declared.timezone.empty() && declared.timezone != get_timezone())
        diffs.push_back({"system", "timezone", DiffAction::Change, get_timezone(), declared.timezone});

    for (const auto& pkg : declared.packages.system_packages) {
        if (!is_package_installed(pkg))
            diffs.push_back({"package", pkg, DiffAction::Install, "not installed", "will install"});
    }

    for (const auto& [svc, state] : declared.services) {
        std::string cur = get_service_state(svc);
        if (state == "enabled" && cur != "enabled")
            diffs.push_back({"service", svc, DiffAction::Enable, cur, "will enable"});
        else if (state == "disabled" && cur != "disabled")
            diffs.push_back({"service", svc, DiffAction::Disable, cur, "will disable"});
    }

    for (const auto& [username, ucfg] : user_configs) {
        for (const auto& [dest, src] : ucfg.dotfiles) {
            std::string cur = "not linked";
            if (std::filesystem::is_symlink(dest))        cur = "linked";
            else if (std::filesystem::exists(dest))       cur = "exists (not symlink)";
            diffs.push_back({"dotfile", dest, DiffAction::Symlink, cur, src});
        }
        for (const auto& [dest, src] : ucfg.symlinks) {
            std::string cur = "not linked";
            if (std::filesystem::is_symlink(dest))        cur = "linked";
            else if (std::filesystem::exists(dest))       cur = "exists (not symlink)";
            diffs.push_back({"symlink", dest, DiffAction::Symlink, cur, src});
        }
    }

    return diffs;
}

void print_diff(const std::vector<DiffEntry>& diffs) {
    if (diffs.empty()) { std::cout << "No changes needed.\n"; return; }
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    for (const auto& d : diffs) {
        const char* tag = "[ ]";
        switch (d.action) {
            case DiffAction::Install: tag = "[+]"; break;
            case DiffAction::Remove:  tag = "[-]"; break;
            case DiffAction::Enable:
            case DiffAction::Disable:
            case DiffAction::Change:  tag = "[~]"; break;
            case DiffAction::Symlink: tag = "[→]"; break;
            case DiffAction::Unlink:  tag = "[-]"; break;
            case DiffAction::Conflict:tag = "[!]"; break;
        }
        std::cout << "  " << tag << "  " << d.name;
        if (!d.target.empty()) std::cout << "  →  " << d.target;
        std::cout << '\n';
    }
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
}

} // namespace astral_sys
