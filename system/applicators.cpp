#include "system/applicators.hpp"
#include "util/process.hpp"
#include "util/file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace astral_sys {

namespace {

bool cmd_exists(const std::string& cmd) {
    return util::run("sh", {"-c", "command -v " + cmd + " >/dev/null 2>&1"}).exit_code == 0;
}

std::string detect_init() {
    if (std::filesystem::exists("/run/systemd/private"))  return "systemd";
    if (std::filesystem::exists("/run/openrc/softlevel")) return "openrc";
    if (std::filesystem::exists("/run/runit"))            return "runit";
    if (std::filesystem::exists("/run/s6"))               return "s6";
    if (std::filesystem::exists("/run/dinit"))            return "dinit";
    return "sysv";
}

} // anonymous namespace

void apply_locale(const I18nConfig& i18n) {
    if (i18n.default_locale.empty()) return;

    // Write /etc/locale.conf (systemd-style, also read by many tools)
    std::ofstream lc("/etc/locale.conf");
    lc << "LANG=" << i18n.default_locale << "\n";
    for (const auto& [k, v] : i18n.extra) lc << k << "=" << v << "\n";

    // Write /etc/locale.gen and regenerate if available
    auto locale_gen = std::filesystem::path("/etc/locale.gen");
    if (std::filesystem::exists(locale_gen)) {
        std::string content = util::read_file(locale_gen);
        // Uncomment or append the needed locale
        std::string bare = i18n.default_locale;
        if (content.find(bare) == std::string::npos) {
            std::ofstream f(locale_gen, std::ios::app);
            f << bare << " UTF-8\n";
        } else {
            // Uncomment if commented
            std::string uncommented;
            std::istringstream ss(content);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.rfind("# " + bare, 0) == 0 || line.rfind("#" + bare, 0) == 0)
                    line = line.substr(line.find(bare));
                uncommented += line + "\n";
            }
            util::write_file(locale_gen, uncommented);
        }
        if (cmd_exists("locale-gen")) util::run("locale-gen", {});
    }

    std::cout << "  locale → " << i18n.default_locale << "\n";
}

void apply_console(const ConsoleConfig& console) {
    auto init = detect_init();

    if (init == "systemd") {
        // /etc/vconsole.conf
        std::ofstream f("/etc/vconsole.conf");
        if (!console.keymap.empty()) f << "KEYMAP=" << console.keymap << "\n";
        if (!console.font.empty())   f << "FONT="   << console.font   << "\n";
    } else {
        // /etc/rc.conf or /etc/conf.d/consolefont + keymaps (OpenRC)
        if (!console.keymap.empty()) {
            auto kc = std::filesystem::path("/etc/conf.d/keymaps");
            if (std::filesystem::exists(kc)) {
                std::ofstream f(kc);
                f << "keymap=\"" << console.keymap << "\"\n";
            }
        }
        if (!console.font.empty()) {
            auto fc = std::filesystem::path("/etc/conf.d/consolefont");
            if (std::filesystem::exists(fc)) {
                std::ofstream f(fc);
                f << "consolefont=\"" << console.font << "\"\n";
            }
        }
    }
    std::cout << "  console: keymap=" << console.keymap
              << " font=" << console.font << "\n";
}

void apply_xserver(bool enable, const std::string& graphics_driver) {
    if (!enable) return;

    std::filesystem::create_directories("/etc/X11/xorg.conf.d");

    if (!graphics_driver.empty()) {
        std::ofstream f("/etc/X11/xorg.conf.d/20-gpu.conf");
        f << "Section \"Device\"\n"
          << "    Identifier  \"GPU\"\n"
          << "    Driver      \"" << graphics_driver << "\"\n"
          << "EndSection\n";
        std::cout << "  xserver driver → " << graphics_driver << "\n";
    }
}

void apply_kernel_modules(const KernelConfig& kernel) {
    // initrd modules → /etc/mkinitcpio.conf or dracut
    if (!kernel.initrd_modules.empty()) {
        auto mkinit = std::filesystem::path("/etc/mkinitcpio.conf");
        if (std::filesystem::exists(mkinit)) {
            std::string content = util::read_file(mkinit);
            std::string mod_line = "MODULES=(";
            for (const auto& m : kernel.initrd_modules) mod_line += m + " ";
            mod_line += ")";

            std::istringstream ss(content);
            std::string out, line;
            bool replaced = false;
            while (std::getline(ss, line)) {
                if (line.rfind("MODULES=", 0) == 0) { out += mod_line + "\n"; replaced = true; }
                else out += line + "\n";
            }
            if (!replaced) out += mod_line + "\n";
            util::write_file(mkinit, out);
            if (cmd_exists("mkinitcpio")) util::run("mkinitcpio", {"-P"});
        }
        // dracut
        auto dracut_dir = std::filesystem::path("/etc/dracut.conf.d");
        if (std::filesystem::exists(dracut_dir)) {
            std::ofstream f(dracut_dir / "astral-env-modules.conf");
            f << "add_drivers+=\"";
            for (const auto& m : kernel.initrd_modules) f << m << " ";
            f << "\"\n";
            if (cmd_exists("dracut")) util::run("dracut", {"--force"});
        }
    }

    // runtime modules → /etc/modules-load.d/astral-env.conf
    if (!kernel.modules.empty()) {
        std::filesystem::create_directories("/etc/modules-load.d");
        std::ofstream f("/etc/modules-load.d/astral-env.conf");
        for (const auto& m : kernel.modules) f << m << "\n";
    }

    std::cout << "  kernel modules: " << kernel.modules.size()
              << " runtime, " << kernel.initrd_modules.size() << " initrd\n";
}

void apply_microcode(const std::string& cpu) {
    if (cpu.empty()) return;
    std::string pkg;
    if (cpu == "amd")   pkg = "amd-ucode";
    if (cpu == "intel") pkg = "intel-ucode";
    if (pkg.empty()) return;

    auto r = util::run("astral", {"-y", "-S", pkg});
    if (r.exit_code == 0)
        std::cout << "  microcode → " << pkg << "\n";
    else
        std::cerr << "  warn: microcode install failed: " << r.stderr_output << "\n";
}

void apply_firmware(bool enable_all) {
    if (!enable_all) return;
    // linux-firmware is the standard package name
    auto r = util::run("astral", {"-y", "-S", "linux-firmware"});
    if (r.exit_code == 0)
        std::cout << "  firmware → linux-firmware\n";
    else
        std::cerr << "  warn: firmware install failed\n";
}

void apply_graphics(const std::string& driver) {
    if (driver.empty()) return;
    // Map driver name to package
    static const std::map<std::string, std::string> pkg_map = {
        {"amdgpu",  "mesa"},
        {"i915",    "mesa"},
        {"nouveau", "mesa"},
        {"nvidia",  "nvidia"},
        {"radeon",  "mesa"},
    };
    auto it = pkg_map.find(driver);
    if (it != pkg_map.end()) {
        util::run("astral", {"-y", "-S", it->second});
        std::cout << "  graphics → " << driver << " (" << it->second << ")\n";
    }
    // Write modprobe blacklist for conflicting drivers
    if (driver == "amdgpu") {
        std::filesystem::create_directories("/etc/modprobe.d");
        std::ofstream f("/etc/modprobe.d/radeon-blacklist.conf");
        f << "blacklist radeon\n";
    }
    if (driver == "nvidia") {
        std::filesystem::create_directories("/etc/modprobe.d");
        std::ofstream f("/etc/modprobe.d/nouveau-blacklist.conf");
        f << "blacklist nouveau\nblacklist lbm-nouveau\noptions nouveau modeset=0\n";
    }
}

void apply_fstab(const std::vector<DiskEntry>& disks) {
    if (disks.empty()) return;

    std::ofstream f("/etc/fstab");
    f << "# Generated by astral-env — edit env.stars to change\n";
    f << "# <device>  <mount>  <fstype>  <options>  <dump>  <pass>\n\n";

    for (const auto& d : disks) {
        std::string opts = "defaults";
        int dump = 0, pass = 0;
        if (d.path == "/")     { pass = 1; }
        if (d.path != "/" && d.path != "/boot" && d.fs_type != "tmpfs") pass = 2;
        if (d.fs_type == "tmpfs") opts = "defaults,nosuid,nodev";

        f << d.disk_path << "\t" << d.path << "\t" << d.fs_type
          << "\t" << opts << "\t" << dump << "\t" << pass << "\n";
    }
    std::cout << "  fstab → " << disks.size() << " entries\n";
}

void apply_networking(const NetworkConfig& net) {
    // Detect network manager
    bool has_nm   = cmd_exists("nmcli");
    bool has_dhcp = cmd_exists("dhcpcd");

    if (net.use_dhcp && net.interfaces.empty()) {
        // Global DHCP — write dhcpcd.conf or enable NetworkManager
        if (has_nm) {
            util::run("astral", {"enable", "NetworkManager"});
            std::cout << "  networking → NetworkManager (global DHCP)\n";
        } else if (has_dhcp) {
            std::ofstream f("/etc/dhcpcd.conf", std::ios::app);
            f << "# astral-env: global DHCP\n";
            std::cout << "  networking → dhcpcd (global DHCP)\n";
        }
        return;
    }

    for (const auto& iface : net.interfaces) {
        if (iface.use_dhcp) {
            if (has_nm) {
                util::run("nmcli", {"con", "add", "type", "ethernet",
                    "ifname", iface.name, "con-name", iface.name});
            } else if (has_dhcp) {
                std::ofstream f("/etc/dhcpcd.conf", std::ios::app);
                f << "interface " << iface.name << "\n    dhcp\n";
            }
            std::cout << "  networking → " << iface.name << " (DHCP)\n";
        } else if (!iface.address.empty()) {
            // Static IP
            if (has_nm) {
                // NetworkManager: create a static connection
                std::vector<std::string> args = {
                    "con", "add", "type", "ethernet",
                    "ifname", iface.name, "con-name", iface.name,
                    "ipv4.method", "manual",
                    "ipv4.addresses", iface.address
                };
                if (!iface.gateway.empty()) {
                    args.push_back("ipv4.gateway");
                    args.push_back(iface.gateway);
                }
                if (!iface.dns.empty()) {
                    std::string dns_str;
                    for (const auto& d : iface.dns) dns_str += d + ",";
                    if (!dns_str.empty()) dns_str.pop_back();
                    args.push_back("ipv4.dns");
                    args.push_back(dns_str);
                }
                util::run("nmcli", args);
            } else if (has_dhcp) {
                // dhcpcd static config
                std::ofstream f("/etc/dhcpcd.conf", std::ios::app);
                f << "interface " << iface.name << "\n";
                f << "    static ip_address=" << iface.address << "\n";
                if (!iface.gateway.empty())
                    f << "    static routers=" << iface.gateway << "\n";
                if (!iface.dns.empty()) {
                    f << "    static domain_name_servers=";
                    for (const auto& d : iface.dns) f << d << " ";
                    f << "\n";
                }
            } else {
                // systemd-networkd fallback
                std::filesystem::create_directories("/etc/systemd/network");
                std::ofstream f("/etc/systemd/network/10-" + iface.name + ".network");
                f << "[Match]\nName=" << iface.name << "\n\n";
                f << "[Network]\nAddress=" << iface.address << "\n";
                if (!iface.gateway.empty()) f << "Gateway=" << iface.gateway << "\n";
                for (const auto& d : iface.dns) f << "DNS=" << d << "\n";
            }
            std::cout << "  networking → " << iface.name
                      << " (static " << iface.address << ")\n";
        }
    }
}

void apply_users(const std::vector<UserEntry>& users) {
    for (const auto& u : users) {
        // Check if user exists
        auto check = util::run("id", {u.name});
        if (check.exit_code == 0) {
            std::cout << "  user: " << u.name << " already exists\n";
        } else {
            std::vector<std::string> args = {"-m"};
            if (!u.home_dir.empty()) { args.push_back("-d"); args.push_back(u.home_dir); }
            args.push_back(u.name);
            auto r = util::run("useradd", args);
            if (r.exit_code != 0) {
                std::cerr << "  FAIL: useradd " << u.name << ": " << r.stderr_output << "\n";
                continue;
            }
            std::cout << "  user: created " << u.name << "\n";
        }

        // Set groups
        for (const auto& g : u.groups) {
            util::run("usermod", {"-aG", g, u.name});
        }
        if (!u.groups.empty())
            std::cout << "  user: " << u.name << " groups: ";
        for (const auto& g : u.groups) std::cout << g << " ";
        if (!u.groups.empty()) std::cout << "\n";
    }
}

void apply_user_shell(const UserConfig& ucfg, const std::string& home) {
    if (home.empty()) return;
    auto home_path = std::filesystem::path(home);

    // Determine rc file based on shell
    std::string shell_name = ucfg.shell;
    auto sl = shell_name.rfind('/');
    if (sl != std::string::npos) shell_name = shell_name.substr(sl + 1);

    std::filesystem::path rc_file;
    if (shell_name == "zsh")  rc_file = home_path / ".zshrc";
    else if (shell_name == "fish") rc_file = home_path / ".config/fish/config.fish";
    else rc_file = home_path / ".bashrc";

    // Aliases
    if (!ucfg.aliases.empty()) {
        std::ofstream f(rc_file, std::ios::app);
        f << "\n# astral-env aliases\n";
        for (const auto& [k, v] : ucfg.aliases)
            f << "alias " << k << "='" << v << "'\n";
        std::cout << "  aliases → " << rc_file << "\n";
    }

    // Vars
    if (!ucfg.vars.empty()) {
        std::ofstream f(rc_file, std::ios::app);
        f << "\n# astral-env vars\n";
        for (const auto& [k, v] : ucfg.vars)
            f << "export " << k << "=\"" << v << "\"\n";
        std::cout << "  vars → " << rc_file << "\n";
    }
}

void apply_aliases(const UserConfig& ucfg) {
    // System-wide aliases in /etc/profile.d/
    if (ucfg.aliases.empty()) return;
    std::filesystem::create_directories("/etc/profile.d");
    std::ofstream f("/etc/profile.d/astral-env-" + ucfg.username + ".sh");
    f << "# astral-env aliases for " << ucfg.username << "\n";
    for (const auto& [k, v] : ucfg.aliases)
        f << "alias " << k << "='" << v << "'\n";
    std::cout << "  aliases: " << ucfg.aliases.size() << " for " << ucfg.username << "\n";
}

void apply_vars(const UserConfig& ucfg) {
    if (ucfg.vars.empty()) return;
    // Write to /etc/environment for system-wide env, user rc for per-user
    std::ofstream f("/etc/environment", std::ios::app);
    f << "# astral-env vars for " << ucfg.username << "\n";
    for (const auto& [k, v] : ucfg.vars)
        f << k << "=\"" << v << "\"\n";
    std::cout << "  vars: " << ucfg.vars.size() << " for " << ucfg.username << "\n";
}

void apply_snapd_boot(const std::string& init) {
    if (init == "systemd") {
        // Write unit file and enable
        std::filesystem::create_directories("/etc/systemd/system");
        std::ofstream f("/etc/systemd/system/astral-env-snapd.service");
        f << "[Unit]\nDescription=astral-env snapshot daemon\nAfter=network.target\n\n"
          << "[Service]\nExecStart=/usr/bin/astral-env-snapd\nRestart=on-failure\n\n"
          << "[Install]\nWantedBy=multi-user.target\n";
        util::run("systemctl", {"daemon-reload"});
        util::run("systemctl", {"enable", "--now", "astral-env-snapd"});
        std::cout << "  snapd → systemd unit enabled\n";
    } else if (init == "openrc") {
        std::filesystem::create_directories("/etc/init.d");
        std::ofstream f("/etc/init.d/astral-env-snapd");
        f << "#!/sbin/openrc-run\n"
          << "description=\"astral-env snapshot daemon\"\n"
          << "command=/usr/bin/astral-env-snapd\n"
          << "command_background=true\n"
          << "pidfile=/run/astral-env-snapd.pid\n";
        util::run("chmod", {"+x", "/etc/init.d/astral-env-snapd"});
        util::run("rc-update", {"add", "astral-env-snapd", "default"});
        util::run("rc-service", {"astral-env-snapd", "start"});
        std::cout << "  snapd → OpenRC service enabled\n";
    } else if (init == "runit") {
        auto sv = std::filesystem::path("/etc/sv/astral-env-snapd");
        std::filesystem::create_directories(sv);
        std::ofstream f(sv / "run");
        f << "#!/bin/sh\nexec /usr/bin/astral-env-snapd\n";
        util::run("chmod", {"+x", (sv / "run").string()});
        std::error_code ec;
        std::filesystem::create_symlink(sv, "/var/service/astral-env-snapd", ec);
        std::cout << "  snapd → runit service linked\n";
    } else if (init == "s6") {
        auto sv = std::filesystem::path("/etc/s6/sv/astral-env-snapd");
        std::filesystem::create_directories(sv);
        std::ofstream f(sv / "run");
        f << "#!/execlineb -P\n/usr/bin/astral-env-snapd\n";
        util::run("chmod", {"+x", (sv / "run").string()});
        std::cout << "  snapd → s6 service created (link manually to bundle)\n";
    } else if (init == "dinit") {
        // dinit: write a service file under /etc/dinit.d/
        std::filesystem::create_directories("/etc/dinit.d");
        std::ofstream f("/etc/dinit.d/astral-env-snapd");
        f << "# astral-env snapshot daemon — managed by astral-env\n";
        f << "type = process\n";
        f << "command = /usr/bin/astral-env-snapd\n";
        f << "restart = true\n";
        f.close();
        // Enable by linking into /etc/dinit.d/boot.d/ if it exists
        auto boot_d = std::filesystem::path("/etc/dinit.d/boot.d");
        if (std::filesystem::exists(boot_d)) {
            std::error_code ec;
            std::filesystem::create_symlink(
                "/etc/dinit.d/astral-env-snapd",
                boot_d / "astral-env-snapd", ec);
        }
        // Start immediately if dinit socket is available
        util::run("dinitctl", {"start", "astral-env-snapd"});
        std::cout << "  snapd → dinit service registered\n";
    } else {
        // SysVinit — write /etc/rc.local entry
        std::ofstream f("/etc/rc.local", std::ios::app);
        f << "/usr/bin/astral-env-snapd &\n";
        std::cout << "  snapd → added to /etc/rc.local\n";
    }
}

void apply_bootloader(const KernelConfig& kernel) {
    if (kernel.loader_type.empty()) return;

    if (kernel.loader_type == "limine") {
        auto cfg = std::filesystem::path("/boot/limine.cfg");
        if (!std::filesystem::exists(cfg)) {
            std::cout << "  bootloader: limine.cfg not found, skipping\n";
            return;
        }
        // Read existing config, replace/append TIMEOUT and KERNEL_CMDLINE
        std::string existing = util::read_file(cfg);
        std::istringstream ss(existing);
        std::string out_cfg, line;
        bool replaced_timeout = false, replaced_cmdline = false;
        while (std::getline(ss, line)) {
            if (kernel.loader_timeout > 0 && line.rfind("TIMEOUT=", 0) == 0) {
                out_cfg += "TIMEOUT=" + std::to_string(kernel.loader_timeout) + "\n";
                replaced_timeout = true;
            } else if (!kernel.params.empty() && line.rfind("KERNEL_CMDLINE=", 0) == 0) {
                out_cfg += "KERNEL_CMDLINE=";
                for (const auto& p : kernel.params) out_cfg += p + " ";
                out_cfg += "\n";
                replaced_cmdline = true;
            } else {
                out_cfg += line + "\n";
            }
        }
        if (kernel.loader_timeout > 0 && !replaced_timeout)
            out_cfg += "TIMEOUT=" + std::to_string(kernel.loader_timeout) + "\n";
        if (!kernel.params.empty() && !replaced_cmdline) {
            out_cfg += "\n# astral-env kernel params\nKERNEL_CMDLINE=";
            for (const auto& p : kernel.params) out_cfg += p + " ";
            out_cfg += "\n";
        }
        util::write_file(cfg, out_cfg);
        std::cout << "  bootloader → limine config updated\n";
    } else if (kernel.loader_type == "grub") {
        auto grub_def = std::filesystem::path("/etc/default/grub");
        if (std::filesystem::exists(grub_def)) {
            std::string grub_content = util::read_file(grub_def);
            std::string param_line = "GRUB_CMDLINE_LINUX_DEFAULT=\"";
            for (const auto& p : kernel.params) param_line += p + " ";
            if (!kernel.params.empty() && param_line.back() == ' ') param_line.pop_back();
            param_line += "\"";

            std::string timeout_line;
            if (kernel.loader_timeout > 0)
                timeout_line = "GRUB_TIMEOUT=" + std::to_string(kernel.loader_timeout);

            std::istringstream ss(grub_content);
            std::string out, line;
            bool replaced_cmdline = false, replaced_timeout = false;
            while (std::getline(ss, line)) {
                if (!kernel.params.empty() && line.rfind("GRUB_CMDLINE_LINUX_DEFAULT=", 0) == 0) {
                    out += param_line + "\n"; replaced_cmdline = true;
                } else if (!timeout_line.empty() && line.rfind("GRUB_TIMEOUT=", 0) == 0) {
                    out += timeout_line + "\n"; replaced_timeout = true;
                } else {
                    out += line + "\n";
                }
            }
            if (!kernel.params.empty() && !replaced_cmdline) out += param_line + "\n";
            if (!timeout_line.empty() && !replaced_timeout)  out += timeout_line + "\n";
            util::write_file(grub_def, out);
            if (cmd_exists("grub-mkconfig"))
                util::run("grub-mkconfig", {"-o", "/boot/grub/grub.cfg"});
        }
        std::cout << "  bootloader → grub config updated\n";
    }
}

} // namespace astral_sys
