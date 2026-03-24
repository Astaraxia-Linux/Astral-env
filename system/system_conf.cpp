#include "system/system_conf.hpp"
#include "env/parser.hpp"
#include "env/node.hpp"

#include <filesystem>
#include <sstream>

namespace astral_sys {

namespace {

using namespace env;

// Collect all bare __item_N values from a NodeMap as a string list
std::vector<std::string> collect_items(const NodeMap& m) {
    std::vector<std::string> out;
    for (const auto& [k, v] : m) {
        if (k.rfind("__item_", 0) == 0 && v.is_str())
            out.push_back(v.str());
    }
    return out;
}

bool str_bool(std::string_view s) {
    return s == "true" || s == "1" || s == "yes" || s == "enabled";
}

void parse_i18n(I18nConfig& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("defaultLocale"); p && (*p)->is_str())
        out.default_locale = (*p)->str();
    if (auto p = n.get("extraLocaleSettings"); p && (*p)->is_map()) {
        for (const auto& [k, v] : (*p)->map()) {
            if (v.is_str()) out.extra[k] = v.str();
        }
    }
}

void parse_console(ConsoleConfig& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("font");   p && (*p)->is_str()) out.font   = (*p)->str();
    if (auto p = n.get("keyMap"); p && (*p)->is_str()) out.keymap = (*p)->str();
}

void parse_repos(PackagesConfig& out, const Node& repo_node) {
    if (!repo_node.is_map()) return;
    for (const auto& [name, v] : repo_node.map()) {
        if (k_is_item(name)) continue;
        RepoConfig r;
        r.name = name;
        if (v.is_map()) {
            if (auto u = v.get("url"); u && (*u)->is_str()) r.url = (*u)->str();
        } else if (v.is_str()) {
            r.url = v.str();
        }
        out.repos.push_back(std::move(r));
    }
}

// Returns true if key is an auto-indexed item
bool k_is_item(const std::string& k) {
    return k.rfind("__item_", 0) == 0;
}

void parse_fonts(FontConfig& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("nerdFonts"); p) {
        if ((*p)->is_map())  out.nerd_fonts = collect_items((*p)->map());
        else if ((*p)->is_list()) {
            for (const auto& item : (*p)->list())
                if (item.is_str()) out.nerd_fonts.push_back(item.str());
        }
    }
}

void parse_packages(PackagesConfig& out, const Node& n) {
    if (!n.is_map()) return;
    const NodeMap& m = n.map();

    if (auto p = n.get("binaryPkg"); p && (*p)->is_str())
        out.binary_pkg = str_bool((*p)->str());
    if (auto p = n.get("Repository"); p) parse_repos(out, **p);
    if (auto p = n.get("System.Fonts"); p) parse_fonts(out.fonts, **p);

    // System.Packages (bare items)
    auto collect_sys_pkgs = [&](const Node& pn) {
        if (pn.is_map()) {
            auto items = collect_items(pn.map());
            out.system_packages.insert(out.system_packages.end(), items.begin(), items.end());
        }
    };
    if (auto p = n.get_path("System.Packages"); p) collect_sys_pkgs(**p);
}

void parse_kernel(KernelConfig& out, const Node& boot) {
    if (!boot.is_map()) return;
    if (auto p = boot.get_path("Initrd.Kernel.Modules"); p) {
        auto& vn = **p;
        if (vn.is_list())
            for (const auto& i : vn.list()) if (i.is_str()) out.initrd_modules.push_back(i.str());
        else if (vn.is_map()) out.initrd_modules = collect_items(vn.map());
    }
    if (auto p = boot.get_path("Kernel.Modules"); p) {
        auto& vn = **p;
        if (vn.is_list())
            for (const auto& i : vn.list()) if (i.is_str()) out.modules.push_back(i.str());
        else if (vn.is_map()) out.modules = collect_items(vn.map());
    }
    if (auto p = boot.get("Loader"); p && (*p)->is_map()) {
        const auto& lm = (*p)->map();
        if (auto t = Node(lm).get("type");    t && (*t)->is_str()) out.loader_type = (*t)->str();
        if (auto t = Node(lm).get("timeout"); t && (*t)->is_str()) {
            try { out.loader_timeout = std::stoi((*t)->str()); } catch (...) {}
        }
        if (auto t = Node(lm).get("kernelParams"); t) {
            const auto& kp = **t;
            if (kp.is_list())
                for (const auto& i : kp.list()) if (i.is_str()) out.params.push_back(i.str());
            else if (kp.is_map()) out.params = collect_items(kp.map());
        }
    }
}

void parse_hardware(HardwareConfig& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("cpu");               p && (*p)->is_str()) out.cpu       = (*p)->str();
    if (auto p = n.get("graphics");          p && (*p)->is_str()) out.graphics  = (*p)->str();
    if (auto p = n.get("enableAllFirmware"); p && (*p)->is_str()) out.all_firmware = str_bool((*p)->str());
}

void parse_filesystems(std::vector<DiskEntry>& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("Disk"); p && (*p)->is_map()) {
        for (const auto& [mount, vn] : (*p)->map()) {
            if (k_is_item(mount)) continue;
            DiskEntry d;
            d.path = mount;
            if (vn.is_map()) {
                if (auto dp = vn.get("diskPath"); dp && (*dp)->is_str()) d.disk_path = (*dp)->str();
                if (auto fs = vn.get("fsType");   fs && (*fs)->is_str()) d.fs_type   = (*fs)->str();
            }
            out.push_back(std::move(d));
        }
    }
}

void parse_networking(NetworkConfig& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("useDHCP"); p && (*p)->is_str()) out.use_dhcp = str_bool((*p)->str());
    if (auto p = n.get("Interface"); p && (*p)->is_map()) {
        for (const auto& [iface, vn] : (*p)->map()) {
            if (k_is_item(iface)) continue;
            NetworkInterface ni;
            ni.name = iface;
            if (vn.is_map()) {
                if (auto d = vn.get("useDHCP");  d && (*d)->is_str()) ni.use_dhcp = str_bool((*d)->str());
                if (auto a = vn.get("address");  a && (*a)->is_str()) ni.address  = (*a)->str();
                if (auto g = vn.get("gateway");  g && (*g)->is_str()) ni.gateway  = (*g)->str();
                if (auto d = vn.get("dns"); d) {
                    const auto& dn = **d;
                    if (dn.is_list())
                        for (const auto& i : dn.list()) if (i.is_str()) ni.dns.push_back(i.str());
                    else if (dn.is_map()) ni.dns = collect_items(dn.map());
                }
            }
            out.interfaces.push_back(std::move(ni));
        }
    }
}

void parse_users(std::vector<UserEntry>& out, const Node& n) {
    if (!n.is_map()) return;
    if (auto p = n.get("Users"); p && (*p)->is_map()) {
        for (const auto& [name, vn] : (*p)->map()) {
            if (k_is_item(name)) continue;
            UserEntry u;
            u.name = name;
            if (vn.is_map()) {
                if (auto x = vn.get("normalUser");    x && (*x)->is_str()) u.normal_user  = str_bool((*x)->str());
                if (auto x = vn.get("homeDir");       x && (*x)->is_str()) u.home_dir     = (*x)->str();
                if (auto x = vn.get("userConfigPath");x && (*x)->is_str()) u.config_path  = (*x)->str();
                if (auto x = vn.get("moreGroup")) {
                    const auto& gn = **x;
                    if (gn.is_list())
                        for (const auto& g : gn.list()) if (g.is_str()) u.groups.push_back(g.str());
                    else if (gn.is_map()) u.groups = collect_items(gn.map());
                }
            }
            out.push_back(std::move(u));
        }
    }
}

SystemConfig build_system(const NodeMap& root) {
    SystemConfig cfg;
    cfg.raw = root;
    Node r(root);

    if (auto p = r.get_path("System.hostName");   p && (*p)->is_str()) cfg.hostname    = (*p)->str();
    if (auto p = r.get_path("System.timeZone");   p && (*p)->is_str()) cfg.timezone    = (*p)->str();
    if (auto p = r.get_path("System.layout");     p && (*p)->is_str()) cfg.layout      = (*p)->str();
    if (auto p = r.get_path("System.xkbVariant"); p && (*p)->is_str()) cfg.xkb_variant = (*p)->str();

    if (auto p = r.get_path("System.i18n");    p) parse_i18n(cfg.i18n, **p);
    if (auto p = r.get_path("System.Console"); p) parse_console(cfg.console, **p);
    if (auto p = r.get_path("System.Server.enable.xserver"); p && (*p)->is_str())
        cfg.xserver = str_bool((*p)->str());

    if (auto p = r.get_path("System.Packages"); p) parse_packages(cfg.packages, **p);

    if (auto p = r.get_path("System.Services"); p && (*p)->is_map()) {
        for (const auto& [svc, vn] : (*p)->map()) {
            if (k_is_item(svc)) continue;
            if (vn.is_str()) cfg.services.emplace_back(svc, vn.str());
        }
    }

    if (auto p = r.get("Boot");        p) parse_kernel(cfg.kernel, **p);
    if (auto p = r.get("Hardware");    p) parse_hardware(cfg.hardware, **p);
    if (auto p = r.get("FileSystems"); p) parse_filesystems(cfg.filesystems, **p);
    if (auto p = r.get("Networking");  p) parse_networking(cfg.network, **p);
    if (auto p = r.get("User");        p) parse_users(cfg.users, **p);

    return cfg;
}

UserConfig build_user(const NodeMap& root, const std::string& username) {
    UserConfig cfg;
    cfg.username = username;
    cfg.raw      = root;
    Node r(root);

    if (auto p = r.get_path("User.name");  p && (*p)->is_str()) cfg.username = (*p)->str();
    if (auto p = r.get_path("User.shell"); p && (*p)->is_str()) cfg.shell    = (*p)->str();

    if (auto p = r.get("Packages"); p && (*p)->is_map())
        cfg.packages = collect_items((*p)->map());

    if (auto p = r.get("Aliases"); p && (*p)->is_map()) {
        for (const auto& [k, v] : (*p)->map()) {
            if (k_is_item(k)) continue;
            if (v.is_str()) cfg.aliases[k] = v.str();
        }
    }

    if (auto p = r.get_path("Config.Dotfiles"); p && (*p)->is_map()) {
        for (const auto& [dest, vn] : (*p)->map()) {
            if (k_is_item(dest)) continue;
            std::string src;
            if (vn.is_str()) src = vn.str();
            else if (vn.is_map()) {
                if (auto pp = vn.get("path"); pp && (*pp)->is_str()) src = (*pp)->str();
            }
            cfg.dotfiles.emplace_back(dest, src);
        }
    }

    if (auto p = r.get_path("Config.Symlinks"); p && (*p)->is_map()) {
        for (const auto& [dest, vn] : (*p)->map()) {
            if (k_is_item(dest)) continue;
            if (vn.is_str()) cfg.symlinks.emplace_back(dest, vn.str());
        }
    }

    if (auto p = r.get("Vars"); p && (*p)->is_map()) {
        for (const auto& [k, v] : (*p)->map()) {
            if (k_is_item(k)) continue;
            if (v.is_str()) cfg.vars[k] = v.str();
        }
    }

    return cfg;
}

} // anonymous namespace

SystemConfig parse_system_config(const std::filesystem::path& path) {
    NodeMap root = env::parse_file(path);
    return build_system(root);
}

UserConfig parse_user_config(const std::filesystem::path& path) {
    NodeMap root = env::parse_file(path);
    std::string username = path.stem().string();
    return build_user(root, username);
}

std::vector<std::pair<std::string, UserConfig>>
load_all_user_configs(const std::filesystem::path& config_dir) {
    std::vector<std::pair<std::string, UserConfig>> out;
    if (!std::filesystem::exists(config_dir)) return out;

    for (const auto& entry : std::filesystem::directory_iterator(config_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".stars") continue;
        if (entry.path().stem() == "env") continue;

        auto cfg = parse_user_config(entry.path());
        if (!cfg.username.empty())
            out.emplace_back(cfg.username, std::move(cfg));
    }
    return out;
}

} // namespace astral_sys
