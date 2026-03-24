#include "system/applier.hpp"
#include "system/differ.hpp"
#include "util/process.hpp"
#include "util/file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace astral_sys {

namespace {

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d_%H:%M");
    return ss.str();
}

void set_hostname(const std::string& h) {
    // Write /etc/hostname directly — works on every init system
    util::write_file("/etc/hostname", h + "\n");
    // Best-effort: also tell the running kernel (harmless if it fails)
    util::run("hostname", {h});
}

void set_timezone(const std::string& tz) {
    auto zone = std::filesystem::path("/usr/share/zoneinfo") / tz;
    if (!std::filesystem::exists(zone)) {
        std::cerr << "warning: timezone file not found: " << zone << "\n";
        return;
    }
    std::error_code ec;
    std::filesystem::remove("/etc/localtime", ec);
    std::filesystem::create_symlink(zone, "/etc/localtime", ec);
    if (ec) std::cerr << "warning: failed to set /etc/localtime: " << ec.message() << "\n";
    // Write /etc/timezone for tools that read it
    util::write_file("/etc/timezone", tz + "\n");
}

enum class InitSystem { Systemd, OpenRC, Runit, S6, Dinit, SysV, Unknown };

InitSystem detect_init() {
    if (std::filesystem::exists("/run/systemd/private"))  return InitSystem::Systemd;
    if (std::filesystem::exists("/run/openrc/softlevel")) return InitSystem::OpenRC;
    if (std::filesystem::exists("/run/runit"))            return InitSystem::Runit;
    if (std::filesystem::exists("/run/s6"))               return InitSystem::S6;
    if (std::filesystem::exists("/run/dinit"))            return InitSystem::Dinit;
    if (std::filesystem::exists("/etc/inittab"))          return InitSystem::SysV;
    return InitSystem::Unknown;
}

int service_action(const std::string& action, const std::string& name) {
    switch (detect_init()) {
        case InitSystem::Systemd:
            return util::run("systemctl", {action, name}).exit_code;
        case InitSystem::OpenRC:
            if (action == "enable")  return util::run("rc-update", {"add",    name, "default"}).exit_code;
            if (action == "disable") return util::run("rc-update", {"delete", name, "default"}).exit_code;
            return 1;
        case InitSystem::Runit:
            if (action == "enable") {
                std::error_code ec;
                std::filesystem::create_symlink(
                    "/etc/sv/" + name, "/var/service/" + name, ec);
                return ec ? 1 : 0;
            }
            if (action == "disable") {
                std::error_code ec;
                std::filesystem::remove("/var/service/" + name, ec);
                return ec ? 1 : 0;
            }
            return 1;
        case InitSystem::Dinit:
            if (action == "enable")  return util::run("dinitctl", {"enable",  name}).exit_code;
            if (action == "disable") return util::run("dinitctl", {"disable", name}).exit_code;
            if (action == "start")   return util::run("dinitctl", {"start",   name}).exit_code;
            if (action == "stop")    return util::run("dinitctl", {"stop",    name}).exit_code;
            return 1;
        case InitSystem::SysV:
            // SysVinit: use service(8) for start/stop, chkconfig/update-rc.d for enable/disable
            if (action == "start")   return util::run("service",  {name, "start"}).exit_code;
            if (action == "stop")    return util::run("service",  {name, "stop"}).exit_code;
            if (action == "enable") {
                // Try chkconfig first (RHEL-style), fall back to update-rc.d (Debian-style)
                if (util::run("chkconfig", {name, "on"}).exit_code == 0)  return 0;
                return util::run("update-rc.d", {name, "defaults"}).exit_code;
            }
            if (action == "disable") {
                if (util::run("chkconfig", {name, "off"}).exit_code == 0) return 0;
                return util::run("update-rc.d", {name, "remove"}).exit_code;
            }
            return 1;
        default:
            std::cerr << "warning: service management not supported for this init system\n";
            return 1;
    }
}

void save_snapshot(const std::vector<DiffEntry>& diffs) {
    static const std::filesystem::path snap_dir("/astral-env/snapshots");
    std::filesystem::create_directories(snap_dir);
    auto path = snap_dir / (current_timestamp() + ".json");
    std::ofstream f(path);

    f << "{\n  \"timestamp\": \"" << current_timestamp() << "\",\n";

    // Services
    f << "  \"services\": [\n";
    bool first = true;
    for (const auto& d : diffs) {
        if (d.type != "service") continue;
        if (!first) f << ",\n";
        first = false;
        f << "    {\"name\": \"" << d.name << "\", \"state\": \"" << d.current << "\"}";
    }
    f << "\n  ],\n";

    // Dotfiles
    f << "  \"dotfiles\": [\n";
    first = true;
    for (const auto& d : diffs) {
        if (d.type != "dotfile" && d.type != "symlink") continue;
        auto dest = std::filesystem::path(d.name);
        bool existed     = std::filesystem::exists(dest) || std::filesystem::is_symlink(dest);
        bool was_symlink = std::filesystem::is_symlink(dest);
        std::string tgt;
        if (was_symlink) {
            std::error_code ec;
            tgt = std::filesystem::read_symlink(dest, ec).string();
        }
        if (!first) f << ",\n";
        first = false;
        f << "    {\"dest\": \"" << d.name << "\""
          << ", \"existed\": "    << (existed     ? "true" : "false")
          << ", \"was_symlink\": " << (was_symlink ? "true" : "false")
          << ", \"target\": \""   << tgt << "\"}";
    }
    f << "\n  ],\n";

    // Hostname / timezone
    std::string hostname, tz;
    {
        std::ifstream hf("/etc/hostname"); std::getline(hf, hostname);
        std::error_code ec;
        auto llt = std::filesystem::read_symlink("/etc/localtime", ec).string();
        if (!ec) {
            auto p = llt.find("zoneinfo/");
            if (p != std::string::npos) tz = llt.substr(p + 9);
        }
        if (tz.empty()) {
            std::ifstream tf("/etc/timezone"); std::getline(tf, tz);
        }
    }
    f << "  \"hostname\": \"" << hostname << "\",\n";
    f << "  \"timezone\": \"" << tz << "\"\n}\n";
}

bool confirm(const std::string& msg, bool yes) {
    if (yes) return true;
    std::cout << msg << " [y/N] ";
    std::string r; std::getline(std::cin, r);
    return r == "y" || r == "Y";
}

} // anonymous namespace

int apply_diff(const std::vector<DiffEntry>& diffs, bool dry_run, bool, bool yes) {
    if (diffs.empty()) { std::cout << "No changes to apply.\n"; return 0; }

    if (!dry_run) save_snapshot(diffs);

    int applied = 0;

    // Collect packages for parallel install
    std::vector<std::string> to_install;
    for (const auto& d : diffs)
        if (d.action == DiffAction::Install) to_install.push_back(d.name);

    if (!dry_run && !to_install.empty()) {
        if (to_install.size() == 1) {
            if (util::run("astral", {"-y", "-S", to_install[0]}).exit_code == 0) {
                std::cout << "Installed " << to_install[0] << "\n";
                ++applied;
            }
        } else {
            std::vector<std::string> args = {"--parallel-build"};
            args.insert(args.end(), to_install.begin(), to_install.end());
            util::run("astral", args);
            for (const auto& pkg : to_install) {
                // Check astral DB directly — no grep -q in pipeline (AGENTS.md)
                bool ok = false;
                static const std::filesystem::path db("/var/lib/astral/db");
                if (std::filesystem::exists(db)) {
                    for (const auto& cat : std::filesystem::directory_iterator(db)) {
                        if (std::filesystem::exists(cat.path() / pkg / "version")) { ok = true; break; }
                    }
                }
                if (ok) { std::cout << "Installed " << pkg << "\n"; ++applied; }
                else    std::cerr << "Failed to install " << pkg << "\n";
            }
        }
    } else if (dry_run) {
        for (const auto& pkg : to_install)
            std::cout << "  [+] would install: " << pkg << "\n";
    }

    for (const auto& d : diffs) {
        if (d.action == DiffAction::Install) continue;

        switch (d.action) {
            case DiffAction::Enable:
                if (dry_run) { std::cout << "  [~] would enable: " << d.name << "\n"; break; }
                if (service_action("enable", d.name) == 0)
                    { std::cout << "Enabled " << d.name << "\n"; ++applied; }
                break;

            case DiffAction::Disable:
                if (dry_run) { std::cout << "  [~] would disable: " << d.name << "\n"; break; }
                if (service_action("disable", d.name) == 0)
                    { std::cout << "Disabled " << d.name << "\n"; ++applied; }
                break;

            case DiffAction::Change:
                if (d.name == "hostname") {
                    if (dry_run) { std::cout << "  [~] would set hostname: " << d.target << "\n"; break; }
                    set_hostname(d.target);
                    std::cout << "Hostname → " << d.target << "\n"; ++applied;
                } else if (d.name == "timezone") {
                    if (dry_run) { std::cout << "  [~] would set timezone: " << d.target << "\n"; break; }
                    set_timezone(d.target);
                    std::cout << "Timezone → " << d.target << "\n"; ++applied;
                }
                break;

            case DiffAction::Symlink: {
                if (dry_run) { std::cout << "  [→] would link: " << d.name << " → " << d.target << "\n"; break; }
                auto dest = std::filesystem::path(d.name);
                std::filesystem::create_directories(dest.parent_path());
                if (std::filesystem::exists(dest) && !std::filesystem::is_symlink(dest)) {
                    if (!confirm("Remove existing file " + d.name + "?", yes)) {
                        std::cout << "Skipped " << d.name << "\n"; continue;
                    }
                    std::filesystem::remove(dest);
                } else if (std::filesystem::is_symlink(dest)) {
                    std::filesystem::remove(dest);
                }
                std::error_code ec;
                std::filesystem::create_symlink(d.target, dest, ec);
                if (!ec) { std::cout << "Linked " << d.name << "\n"; ++applied; }
                else std::cerr << "Failed to link " << d.name << ": " << ec.message() << "\n";
                break;
            }

            default: break;
        }
    }

    return applied;
}

bool rollback(const std::string& snapshot_id) {
    static const std::filesystem::path snap_dir("/astral-env/snapshots");
    if (!std::filesystem::exists(snap_dir)) { std::cerr << "No snapshots.\n"; return false; }

    std::filesystem::path snap_path;
    if (snapshot_id.empty()) {
        // Find most recent
        for (const auto& e : std::filesystem::directory_iterator(snap_dir)) {
            if (e.is_regular_file() &&
                (snap_path.empty() || e.path().filename() > snap_path.filename()))
                snap_path = e.path();
        }
    } else {
        snap_path = snap_dir / (snapshot_id + ".json");
    }

    if (!std::filesystem::exists(snap_path)) {
        std::cerr << "Snapshot not found: " << snap_path << "\n";
        return false;
    }

    std::string content = util::read_file(snap_path);
    int restored = 0, failed = 0;

    auto extract_str = [&](std::string_view field, std::size_t from = 0) -> std::string {
        auto pos = content.find(std::string("\"") + std::string(field) + "\"", from);
        if (pos == std::string::npos) return {};
        auto q1 = content.find('"', pos + field.size() + 2);
        if (q1 == std::string::npos) return {};
        auto q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) return {};
        return content.substr(q1 + 1, q2 - q1 - 1);
    };

    // Hostname
    auto h = extract_str("hostname");
    if (!h.empty()) {
        set_hostname(h);
        std::cout << "  Restored hostname → " << h << "\n"; ++restored;
    }

    // Timezone
    auto tz = extract_str("timezone");
    if (!tz.empty()) {
        set_timezone(tz);
        std::cout << "  Restored timezone → " << tz << "\n"; ++restored;
    }

    // Services — walk "services" array
    {
        auto sa = content.find("\"services\"");
        auto se = content.find(']', sa);
        if (sa != std::string::npos && se != std::string::npos) {
            std::string blk = content.substr(sa, se - sa);
            std::size_t cur = 0;
            while (true) {
                auto np = blk.find("\"name\":", cur);
                if (np == std::string::npos) break;
                auto q1 = blk.find('"', np + 7); auto q2 = blk.find('"', q1 + 1);
                std::string svc = blk.substr(q1 + 1, q2 - q1 - 1);
                auto sp = blk.find("\"state\":", q2);
                auto s1 = blk.find('"', sp + 8); auto s2 = blk.find('"', s1 + 1);
                std::string state = blk.substr(s1 + 1, s2 - s1 - 1);
                cur = s2 + 1;
                if (svc.empty()) continue;
                std::string act = (state == "enabled") ? "enable" : "disable";
                if (service_action(act, svc) == 0)
                    { std::cout << "  Restored service " << svc << " → " << state << "\n"; ++restored; }
                else
                    { std::cerr << "  Failed: service " << svc << "\n"; ++failed; }
            }
        }
    }

    // Dotfiles — restore symlinks to their prior state
    {
        auto da = content.find("\"dotfiles\"");
        auto de = content.find(']', da);
        if (da != std::string::npos && de != std::string::npos) {
            std::string blk = content.substr(da, de - da);
            std::size_t cur = 0;
            while (true) {
                auto dp = blk.find("\"dest\":", cur);
                if (dp == std::string::npos) break;
                auto q1 = blk.find('"', dp + 7); auto q2 = blk.find('"', q1 + 1);
                std::string dest_s = blk.substr(q1 + 1, q2 - q1 - 1);
                bool existed     = blk.find("\"existed\": true",     q2) < blk.find("\"dest\":", q2 + 1);
                bool was_symlink = blk.find("\"was_symlink\": true", q2) < blk.find("\"dest\":", q2 + 1);
                auto tp = blk.find("\"target\":", q2);
                auto t1 = blk.find('"', tp + 9); auto t2 = blk.find('"', t1 + 1);
                std::string tgt = blk.substr(t1 + 1, t2 - t1 - 1);
                cur = t2 + 1;

                auto dest = std::filesystem::path(dest_s);
                try {
                    if (!existed) {
                        std::error_code ec;
                        std::filesystem::remove(dest, ec);
                        std::cout << "  Removed " << dest_s << "\n"; ++restored;
                    } else if (was_symlink && !tgt.empty()) {
                        std::error_code ec;
                        if (std::filesystem::is_symlink(dest) || std::filesystem::exists(dest))
                            std::filesystem::remove(dest, ec);
                        std::filesystem::create_symlink(tgt, dest, ec);
                        if (!ec) { std::cout << "  Restored " << dest_s << " → " << tgt << "\n"; ++restored; }
                        else     { std::cerr << "  Failed: " << dest_s << "\n"; ++failed; }
                    } else if (existed && !was_symlink) {
                        std::cerr << "  Warning: " << dest_s << " was a regular file — cannot restore\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "  Error: " << dest_s << ": " << e.what() << "\n"; ++failed;
                }
            }
        }
    }

    std::cout << "\nRollback: " << restored << " restored";
    if (failed) std::cout << ", " << failed << " failed";
    std::cout << "\n";
    return failed == 0;
}

std::vector<std::string> list_snapshots() {
    static const std::filesystem::path snap_dir("/astral-env/snapshots");
    std::vector<std::string> out;
    if (!std::filesystem::exists(snap_dir)) return out;
    for (const auto& e : std::filesystem::directory_iterator(snap_dir))
        if (e.is_regular_file()) out.push_back(e.path().stem().string());
    std::sort(out.rbegin(), out.rend());
    return out;
}

bool rollback_implemented() { return true; }

} // namespace astral_sys

// apply_full — wires all the applicators that aren't diff-driven
#include "system/applicators.hpp"
#include "repo/installer.hpp"

namespace astral_sys {

int apply_full(const SystemConfig& cfg,
               const std::vector<std::pair<std::string, UserConfig>>& user_configs,
               const std::vector<repo::RepoEntry>& repos,
               bool dry_run,
               bool yes) {
    if (dry_run) {
        std::cout << "  [dry-run] would apply full system config\n";
        return 0;
    }

    int failures = 0;

    // Diff-driven changes first
    auto diffs = diff_system(cfg, user_configs);
    apply_diff(diffs, false, false, yes);

    // System-level applicators
    apply_locale(cfg.i18n);
    apply_console(cfg.console);
    apply_xserver(cfg.xserver, cfg.hardware.graphics);
    apply_kernel_modules(cfg.kernel);
    apply_microcode(cfg.hardware.cpu);
    apply_firmware(cfg.hardware.all_firmware);
    apply_graphics(cfg.hardware.graphics);
    apply_fstab(cfg.filesystems);
    apply_networking(cfg.network);
    apply_users(cfg.users);
    apply_bootloader(cfg.kernel);

    // Install packages + fonts
    failures += repo::install_all(cfg, repos);

    // User configs
    for (const auto& [username, ucfg] : user_configs) {
        std::string home = "/home/" + username;
        for (const auto& u : cfg.users)
            if (u.name == username && !u.home_dir.empty()) home = u.home_dir;

        apply_user_shell(ucfg, home);
        apply_aliases(ucfg);
        apply_vars(ucfg);

        auto user_store = std::filesystem::path("/astral-env/users") / username;
        failures += repo::install_user_pkgs(ucfg, repos, user_store);

        // Dotfiles + symlinks
        for (const auto& [dest, src] : ucfg.dotfiles) {
            auto dp = std::filesystem::path(dest);
            std::filesystem::create_directories(dp.parent_path());
            std::error_code ec;
            if (std::filesystem::is_symlink(dp) || std::filesystem::exists(dp))
                std::filesystem::remove(dp, ec);
            std::filesystem::create_symlink(src, dp, ec);
            if (ec) { std::cerr << "  FAIL symlink " << dest << ": " << ec.message() << "\n"; ++failures; }
            else std::cout << "  → " << dest << "\n";
        }
        for (const auto& [dest, src] : ucfg.symlinks) {
            auto dp = std::filesystem::path(dest);
            std::filesystem::create_directories(dp.parent_path());
            std::error_code ec;
            if (std::filesystem::is_symlink(dp) || std::filesystem::exists(dp))
                std::filesystem::remove(dp, ec);
            std::filesystem::create_symlink(src, dp, ec);
            if (ec) { std::cerr << "  FAIL symlink " << dest << ": " << ec.message() << "\n"; ++failures; }
            else std::cout << "  → " << dest << "\n";
        }
    }

    // Start snapd if Snap config exists
    if (!cfg.raw.empty()) {
        env::Node r(cfg.raw);
        if (auto p = r.get("Snap"); p && (*p)->is_map()) {
            apply_snapd_boot(detect_init_sys());
        }
    }

    return failures;
}

// Expose detect_init for apply_full
std::string detect_init_sys() {
    if (std::filesystem::exists("/run/systemd/private"))  return "systemd";
    if (std::filesystem::exists("/run/openrc/softlevel")) return "openrc";
    if (std::filesystem::exists("/run/runit"))            return "runit";
    if (std::filesystem::exists("/run/s6"))               return "s6";
    if (std::filesystem::exists("/run/dinit"))            return "dinit";
    return "sysv";
}

} // namespace astral_sys
