#include "config/conf.hpp"
#include "store/store.hpp"
#include "store/entry.hpp"
#include "lock/lockfile.hpp"
#include "env/shell.hpp"
#include "env/parser.hpp"
#include "gc/gc.hpp"
#include "system/differ.hpp"
#include "system/applier.hpp"
#include "system/system_conf.hpp"
#include "snap/snap.hpp"
#include "util/process.hpp"
#include "repo/registry.hpp"
#include "repo/index.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

// Extern declaration for the daemon main function
extern "C" void run_daemon();

namespace {


void print_version() {
    std::cout << "astral-env " << config::version() << "\n";
}

void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " <command> [options]\n"
              << "\n"
              << "Environment Commands:\n"
              << "  init                  Scaffold astral-env.stars in current directory\n"
              << "  lock [--update [pkg]] Generate/update lockfile from .stars\n"
              << "  build [--force]       Build environment from lockfile\n"
              << "  shell [--dir <d>]     Enter interactive environment shell\n"
              << "  run <cmd...>          Run command inside the environment\n"
              << "  status                Show what's installed vs missing\n"
              << "\n"
              << "System Commands:\n"
              << "  system init           Create /etc/astral/env/env.stars
  system hw-init        Detect hardware and write /etc/astral/env/hw.stars\n"
              << "  system init-user <u>  Create per-user config + dotfiles directory\n"
              << "  system diff           Show pending changes\n"
              << "  system apply          Apply changes (--dry-run, --yes, --user, --global-only)\n"
              << "  system rollback       Roll back (--list, --to <id>)\n"
              << "  system sync-index     Refresh repo index cache\n"
              << "  system check          Validate all .stars files\n"
              << "\n"
              << "Snapshot Commands:\n"
              << "  snap <path>           Snapshot a file or directory\n"
              << "  snap list [path]      List snapshots (optionally filtered by path)\n"
              << "  snap restore <id>     Restore a snapshot (--dest <path> for alternate location)\n"
              << "  snap prune            Prune old snapshots (--keep-last N, --older-than Nd)\n"
              << "\n"
              << "Store Commands:\n"
              << "  store list            List all store entries\n"
              << "  store size            Show total store disk usage\n"
              << "  gc [--dry-run]        Garbage collect unused entries (--max-age <days>)\n"
              << "\n"
              << "Daemon Commands:\n"
              << "  snapd start           Start snapshot daemon (enables at boot)\n"
              << "  snapd stop            Stop snapshot daemon\n"
              << "  snapd restart         Restart snapshot daemon\n"
              << "  snapd status          Show daemon status\n"
              << "\n"
              << "Global Options:\n"
              << "  -v, --verbose         Verbose output\n"
              << "  -q, --quiet           Quiet output\n"
              << "  -V, --version         Show version\n"
              << "  -h, --help            Show this help\n";
}

int cmd_init(const std::vector<std::string>&) {
    auto stars_path = std::filesystem::path("astral-env.stars");

    if (std::filesystem::exists(stars_path)) {
        std::cout << "astral-env.stars already exists, not overwriting.\n";
        return 0;
    }

    std::string content = R"($ENV: {
    Metadata: {
        Name        = "my-project"
        Description = "Dev environment"
    };

    Packages: {
        # Add packages here, e.g.:
        # python >= 3.11
        # nodejs >= 20.0
    };

    Vars: {
        # DEBUG = "true"
    };

    Shell: {
        # echo "entering environment"
    };
};
)";

    std::ofstream f(stars_path);
    f << content;
    std::cout << "Created astral-env.stars\n";

    return 0;
}

int cmd_lock(const std::vector<std::string>& args) {
    bool update_all = false;
    std::vector<std::string> update_packages;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--update") {
            if (i + 1 < args.size() && args[i+1] != "--update") {
                update_packages.push_back(args[++i]);
            } else {
                update_all = true;
            }
        }
    }

    auto stars_path = std::filesystem::path("astral-env.stars");
    auto lock_path = std::filesystem::path("astral-env.lock");
    auto repo_bin = std::filesystem::path("/usr/share/astral/bin");

    try {
        auto lock = lock::generate(stars_path, repo_bin, update_all, update_packages);
        lock::write(lock, lock_path);
        std::cout << "Wrote " << lock_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_build(const std::vector<std::string>& args) {
    bool force = false;

    for (const auto& arg : args) {
        if (arg == "--force") force = true;
    }

    auto lock_path = std::filesystem::path("astral-env.lock");
    auto store_root = std::filesystem::path("/astral-env/store");

    if (!std::filesystem::exists(lock_path)) {
        std::cerr << "Error: no astral-env.lock found.\n";
        std::cerr << "       Run 'astral-env lock' first to generate it.\n";
        return 1;
    }

    try {
        auto lock = lock::read(lock_path);

        for (const auto& entry : lock.entries) {
            // Now we can use lock::LockEntry directly since it's the same as store::LockEntry
            auto result = store::install(entry, store_root, 4, force);
            if (!result) {
                std::cerr << "Failed to install " << entry.name << "\n";
                return 1;
            }
        }

        std::cout << "Build complete.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_shell(const std::vector<std::string>& args) {
    std::filesystem::path dir = std::filesystem::current_path();

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--dir" && i + 1 < args.size()) {
            dir = args[++i];
        }
    }

    auto lock_path = dir / "astral-env.lock";
    auto stars_path = dir / "astral-env.stars";

    if (!std::filesystem::exists(lock_path)) {
        std::cerr << "Error: no astral-env.lock found in " << dir << "\n";
        return 1;
    }

    try {
        env::enter_shell(lock_path, stars_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_run(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Error: no command specified\n";
        return 1;
    }

    auto lock_path = std::filesystem::path("astral-env.lock");
    auto stars_path = std::filesystem::path("astral-env.stars");

    if (!std::filesystem::exists(lock_path)) {
        std::cerr << "Error: no astral-env.lock found.\n";
        return 1;
    }

    try {
        return env::run_command(lock_path, stars_path, args);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_status(const std::vector<std::string>&) {
    auto lock_path = std::filesystem::path("astral-env.lock");
    auto store_root = std::filesystem::path("/astral-env/store");

    if (!std::filesystem::exists(lock_path)) {
        std::cout << "No lockfile found.\n";
        return 0;
    }

    auto lock = lock::read(lock_path);

    std::cout << "astral-env status\n";
    std::cout << "Lock file: " << lock_path << "\n";
    std::cout << "Packages:\n";

    for (const auto& entry : lock.entries) {
        std::cout << "  [";

        // FIX: Check build_kind first - system installs don't go to store
        if (entry.build_kind == lock::BuildKind::System) {
            // System packages are installed via astral -S, not the store
            std::cout << "✓] " << entry.name;
            if (!entry.version.empty()) {
                std::cout << " " << entry.version;
            }
            std::cout << " (system install via astral)\n";
        } else {
            bool present = store::entry_exists(entry.store_path);
            std::cout << (present ? "✓" : "✗") << "] " << entry.name << " " << entry.version;
            if (!present) {
                std::cout << " (store: MISSING — run astral-env build)";
            } else {
                std::cout << " (store: present)";
            }
            std::cout << "\n";
        }
    }

    return 0;
}

int cmd_gc(const std::vector<std::string>& args) {
    bool dry_run = false;  // Default: actually collect (opposite of before)
    int gc_keep_days = 30;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--dry-run") {
            dry_run = true;
        } else if (args[i] == "--max-age" && i + 1 < args.size()) {
            gc_keep_days = std::stoi(args[++i]);
        }
    }

    auto store_root = std::filesystem::path("/astral-env/store");

    // Search multiple roots for lockfiles - not just /home
    std::vector<std::filesystem::path> search_roots;
    const char* home = std::getenv("HOME");
    if (home) search_roots.emplace_back(home);
    search_roots.emplace_back("/home");
    search_roots.emplace_back("/root");
    // Future: read from /astral-env/registry

    // Collect lockfiles from all search roots
    std::vector<std::filesystem::path> all_lockfiles;
    for (const auto& root : search_roots) {
        auto lockfiles = gc::find_lockfiles(root);
        all_lockfiles.insert(all_lockfiles.end(), lockfiles.begin(), lockfiles.end());
    }

    try {
        // FIX: Use multiple search roots, not just one
        auto candidates = gc::find_candidates_multi(store_root, all_lockfiles, gc_keep_days);

        if (candidates.empty()) {
            std::cout << "No GC candidates found.\n";
            return 0;
        }

        uint64_t total_size = 0;
        for (const auto& c : candidates) {
            total_size += c.size;
        }

        std::cout << "Candidates for collection:\n";
        for (const auto& c : candidates) {
            std::cout << "  " << c.path.filename().string() << " - "
                      << (c.size / 1024 / 1024) << " MB\n";
        }
        std::cout << "Total: " << (total_size / 1024 / 1024) << " MB\n";

        if (dry_run) {
            std::cout << "Run without --dry-run to collect these entries.\n";
        } else {
            auto freed = gc::collect(store_root, candidates);
            std::cout << "Freed " << (freed / 1024 / 1024) << " MB\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_snap(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: astral-env snap <path>\n";
        return 1;
    }

    std::filesystem::path path = args[0];

    if (!std::filesystem::exists(path)) {
        std::cerr << "Error: Path does not exist: " << path << "\n";
        return 1;
    }

    try {
        auto result = snap::create(path, snap::SNAP_STORE, snap::SNAP_INDEX, "manual");

        std::cout << "Created snapshot: " << result.snapshot_id << "\n";
        std::cout << "  Original: " << (result.original_bytes / 1024) << " KB\n";
        std::cout << "  Compressed: " << (result.compressed_bytes / 1024) << " KB\n";
        if (result.deduped) {
            std::cout << "  (deduplicated - content unchanged)\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_snap_list(const std::vector<std::string>& args) {
    std::optional<std::filesystem::path> filter_path;

    if (!args.empty()) {
        filter_path = args[0];
    }

    try {
        auto snapshots = snap::list(snap::SNAP_INDEX, filter_path);

        if (snapshots.empty()) {
            std::cout << "No snapshots found.\n";
            return 0;
        }

        if (filter_path) {
            std::cout << "Snapshots for: " << *filter_path << "\n";
        } else {
            std::cout << "All snapshots:\n";
        }

        for (const auto& s : snapshots) {
            std::cout << "  " << s.id << "  " << s.path.string() << " (" << s.reason << ")\n";
        }

        std::cout << "Total: " << snapshots.size() << " snapshots\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_snap_restore(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: astral-env snap restore <id> [--dest <path>]\n";
        return 1;
    }

    std::string snap_id = args[0];
    std::optional<std::filesystem::path> dest;

    // Check for --dest
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == "--dest" && i + 1 < args.size()) {
            dest = args[i + 1];
            break;
        }
    }

    try {
        snap::restore(snap_id, snap::SNAP_INDEX, snap::SNAP_STORE, dest);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_snap_prune(const std::vector<std::string>&) {
    try {
        int pruned = snap::prune(snap::SNAP_INDEX, snap::SNAP_STORE, 30, 5);
        std::cout << "Pruned " << pruned << " snapshots\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

int cmd_store_list(const std::vector<std::string>&) {
    auto store_root = std::filesystem::path("/astral-env/store");

    try {
        auto entries = store::list_store_entries(store_root);

        std::cout << "Store entries:\n";
        for (const auto& entry : entries) {
            std::cout << "  " << entry.filename().string() << "\n";
        }
        std::cout << "Total: " << entries.size() << " entries\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_store_size(const std::vector<std::string>&) {
    auto store_root = std::filesystem::path("/astral-env/store");

    try {
        auto entries = store::list_store_entries(store_root);
        auto size = store::store_size(store_root);

        std::cout << "Store: " << entries.size() << " entries, "
                  << (size / 1024 / 1024) << " MB total\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_system_hw_init(const std::vector<std::string>& args) {
    bool force = false;
    for (const auto& a : args)
        if (a == "--force" || a == "-f") force = true;

    auto env_dir  = std::filesystem::path("/etc/astral/env");
    auto hw_path  = env_dir / "hw.stars";

    if (std::filesystem::exists(hw_path) && !force) {
        std::cout << hw_path << " already exists (use --force to overwrite).\n";
        return 0;
    }

    // ── CPU detection ─────────────────────────────────────────────────────────
    std::string cpu_brand;
    {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("vendor_id", 0) == 0) {
                if (line.find("AuthenticAMD") != std::string::npos)  cpu_brand = "amd";
                else if (line.find("GenuineIntel") != std::string::npos) cpu_brand = "intel";
                break;
            }
        }
    }

    // ── GPU detection ─────────────────────────────────────────────────────────
    std::string gpu_brand;
    {
        auto r = util::run("lspci", {});
        if (r.exit_code == 0) {
            auto& o = r.stdout_output;
            if (o.find("NVIDIA") != std::string::npos || o.find("nvidia") != std::string::npos)
                gpu_brand = "nvidia";
            else if (o.find("Advanced Micro Devices") != std::string::npos ||
                     o.find("Radeon")                 != std::string::npos ||
                     o.find("amdgpu")                 != std::string::npos)
                gpu_brand = "amdgpu";
            else if (o.find("Intel") != std::string::npos &&
                     (o.find("VGA") != std::string::npos || o.find("Display") != std::string::npos))
                gpu_brand = "intel";
        }
    }

    // ── Build hw.stars ────────────────────────────────────────────────────────
    std::ostringstream out;
    out << "# Auto-generated by: astral-env system hw-init\n";
    out << "# Edit as needed; include in env.stars with:\n";
    out << "#   Includes: [ \"./hw.stars\" ];\n\n";
    out << "$ENV: {\n";
    out << "    Hardware: {\n";
    if (!cpu_brand.empty())
        out << "        cpu               = \"" << cpu_brand << "\"\n";
    else
        out << "        # cpu             = \"amd\"  # or \"intel\"\n";
    if (!gpu_brand.empty())
        out << "        graphics          = \"" << gpu_brand << "\"\n";
    else
        out << "        # graphics        = \"amdgpu\"  # or \"nvidia\", \"intel\"\n";
    out << "        enableAllFirmware = \"true\"\n";
    out << "    };\n";
    out << "};\n";

    if (!std::filesystem::exists(env_dir))
        std::filesystem::create_directories(env_dir);

    std::ofstream f(hw_path);
    f << out.str();

    std::cout << "Created " << hw_path << "\n";
    if (!cpu_brand.empty()) std::cout << "  cpu:      " << cpu_brand << "\n";
    if (!gpu_brand.empty()) std::cout << "  graphics: " << gpu_brand << "\n";
    std::cout << "Don't forget to add  Includes: [ \"./hw.stars\" ];  in your env.stars\n";
    return 0;
}

int cmd_system_diff(const std::vector<std::string>& args) {
    bool global_only = false;
    std::string target_user;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--global-only") global_only = true;
        else if (args[i] == "--user" && i + 1 < args.size()) target_user = args[++i];
    }

    auto env_path = std::filesystem::path("/etc/astral/env/env.stars");
    auto env_dir = std::filesystem::path("/etc/astral/env");

    if (!std::filesystem::exists(env_path)) {
        std::cerr << "Error: " << env_path << " not found.\n";
        return 1;
    }

    try {
        auto config = astral_sys::parse_system_config(env_path);

        // FIX: Load user configs
        std::vector<std::pair<std::string, astral_sys::UserConfig>> user_configs;
        if (!global_only) {
            user_configs = astral_sys::load_all_user_configs(env_dir);

            // Filter to target user if specified
            if (!target_user.empty()) {
                std::vector<std::pair<std::string, astral_sys::UserConfig>> filtered;
                for (auto& uc : user_configs) {
                    if (uc.first == target_user) {
                        filtered.push_back(uc);
                        break;
                    }
                }
                user_configs = filtered;
            }
        }

        auto diffs = astral_sys::diff_system(config, user_configs);
        astral_sys::print_diff(diffs);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_system_apply(const std::vector<std::string>& args) {
    bool dry_run = false;
    bool prune = false;
    bool yes = false;
    bool global_only = false;
    std::string target_user;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--dry-run") dry_run = true;
        else if (args[i] == "--prune") prune = true;
        else if (args[i] == "--yes") yes = true;
        else if (args[i] == "--user" && i + 1 < args.size()) target_user = args[++i];
        else if (args[i] == "--global-only") global_only = true;
    }

    auto env_path = std::filesystem::path("/etc/astral/env/env.stars");
    auto env_dir = std::filesystem::path("/etc/astral/env");

    if (!std::filesystem::exists(env_path)) {
        std::cerr << "Error: " << env_path << " not found.\n";
        std::cerr << "Run 'astral-env system init' first.\n";
        return 1;
    }

    try {
        // Parse global config
        auto config = astral_sys::parse_system_config(env_path);

        // Load user configs
        std::vector<std::pair<std::string, astral_sys::UserConfig>> user_configs;
        if (!global_only) {
            user_configs = astral_sys::load_all_user_configs(env_dir);

            // Filter to target user if specified
            if (!target_user.empty()) {
                std::vector<std::pair<std::string, astral_sys::UserConfig>> filtered;
                for (auto& uc : user_configs) {
                    if (uc.first == target_user) {
                        filtered.push_back(uc);
                        break;
                    }
                }
                user_configs = filtered;
            }
        }

        // Compute diff
        auto diffs = astral_sys::diff_system(config, user_configs);

        // Show diff
        std::cout << "astral-env system apply\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        if (diffs.empty()) {
            std::cout << "No changes needed.\n";
            return 0;
        }

        for (const auto& d : diffs) {
            // Convert action enum to string
            std::string action_str;
            switch (d.action) {
                case astral_sys::DiffAction::Install: action_str = "+"; break;
                case astral_sys::DiffAction::Remove: action_str = "-"; break;
                case astral_sys::DiffAction::Enable: action_str = "~"; break;
                case astral_sys::DiffAction::Disable: action_str = "~"; break;
                case astral_sys::DiffAction::Change: action_str = "~"; break;
                case astral_sys::DiffAction::Symlink: action_str = "+"; break;
                case astral_sys::DiffAction::Unlink: action_str = "-"; break;
                case astral_sys::DiffAction::Conflict: action_str = "!"; break;
                default: action_str = "?"; break;
            }

            std::cout << "  [" << action_str << "] " << d.name << "\n";
            if (!d.target.empty()) {
                std::cout << "      -> " << d.target << "\n";
            }
        }

        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        // Ask for confirmation unless --yes
        if (!yes) {
            std::cout << "Apply these changes? [y/N] ";
            std::string response;
            std::getline(std::cin, response);
            if (response != "y" && response != "Y") {
                std::cout << "Aborted.\n";
                return 0;
            }
        }

        if (dry_run) {
            std::cout << "Dry run — no changes made.\n";
            return 0;
        }

        int applied = astral_sys::apply_diff(diffs, false, prune, yes);
        std::cout << "Applied " << applied << " change(s).\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_system_rollback(const std::vector<std::string>& args) {
    std::string snapshot_id;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--list") {
            auto snapshots = astral_sys::list_snapshots();
            std::cout << "Available snapshots:\n";
            for (const auto& s : snapshots) {
                std::cout << "  " << s << "\n";
            }
            return 0;
        } else if (args[i] == "--to" && i + 1 < args.size()) {
            snapshot_id = args[++i];
        }
    }

    try {
        // Check if rollback is actually implemented
        if (!astral_sys::rollback_implemented()) {
            std::cerr << "WARNING: rollback is not yet fully implemented.\n";
            std::cerr << "Only partial restoration is supported.\n";
            return 1;
        }

        if (astral_sys::rollback(snapshot_id)) {
            std::cout << "Rollback complete.\n";
        } else {
            std::cerr << "Rollback failed.\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_system_init(const std::vector<std::string>&) {
    auto env_dir = std::filesystem::path("/etc/astral/env");
    auto env_path = env_dir / "env.stars";

    // Create directory if it doesn't exist with permissions writable by group (2775)
    if (!std::filesystem::exists(env_dir)) {
        std::filesystem::create_directories(env_dir);
        // Set permissions: rwxrwsr-x (2775) - owner and group can read/write/execute, others can read/execute
        // Also setgid bit so new files inherit group ownership
        std::filesystem::permissions(env_dir,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_all |
            std::filesystem::perms::others_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::set_gid,
            std::filesystem::perm_options::replace);
        std::cout << "Created " << env_dir << "\n";
    }

    if (std::filesystem::exists(env_path)) {
        std::cout << env_path << " already exists, not overwriting.\n";
        return 0;
    }

    // Create default env.stars
    std::string content = R"($ENV: {
    System: {
        # hostName = "my-machine"
        # timeZone = "UTC"

        Packages: {
            # neovim
            # git
            # htop
        };

        # Services — format: name = "enabled" | "disabled" | "masked"
        Services: {
            # sshd = "enabled"
        };
    };

    # Hardware detected by: astral-env system hw-init
    # Includes: [ "./hw.stars" ];

    Repository: {
        AOHARU: {
            url = "github::Astaraxia-Linux/AOHARU"
            Priority: { order = "1"; options: []; };
        };
    };
};
)";

    std::ofstream f(env_path);
    f << content;
    // Set file permissions: rw-rw-r-- (664) - owner and group can read/write
    std::filesystem::permissions(env_path,
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_read |
        std::filesystem::perms::group_write |
        std::filesystem::perms::others_read);
    std::cout << "Created " << env_path << "\n";

    // Also create the dotfiles subdirectory with group write permissions
    auto dotfiles_dir = env_dir / "dotfiles";
    if (!std::filesystem::exists(dotfiles_dir)) {
        std::filesystem::create_directories(dotfiles_dir);
        std::filesystem::permissions(dotfiles_dir,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_all |
            std::filesystem::perms::others_exec |
            std::filesystem::perms::others_read);
        std::cout << "Created " << dotfiles_dir << "\n";
    }

    return 0;
}

int cmd_system_init_user(const std::vector<std::string>& args) {
    // Get the username - either from argument or from environment
    std::string username;

    if (!args.empty()) {
        username = args[0];
    } else {
        // Try to get username from environment
        const char* user = std::getenv("USER");
        if (!user) {
            user = std::getenv("USERNAME");
        }
        if (!user) {
            // Try to get from whoami
            auto result = util::run("whoami", {});
            username = result.stdout_output;
            if (!username.empty() && username.back() == '\n') {
                username.pop_back();
            }
        } else {
            username = user;
        }
    }

    if (username.empty()) {
        std::cerr << "Error: Could not determine username. Please specify as argument.\n";
        return 1;
    }

    auto env_dir = std::filesystem::path("/etc/astral/env");
    auto user_path = env_dir / (username + ".stars");

    // Ensure env directory exists
    if (!std::filesystem::exists(env_dir)) {
        std::filesystem::create_directories(env_dir);
        std::filesystem::permissions(env_dir,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_all |
            std::filesystem::perms::others_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::set_gid,
            std::filesystem::perm_options::replace);
        std::cout << "Created " << env_dir << "\n";
    }

    if (std::filesystem::exists(user_path)) {
        std::cout << user_path << " already exists, not overwriting.\n";
        return 0;
    }

    // Create user-specific config
    std::string content = R"($ENV: {
    User: {
        name  = ")" + username + R"("
        shell = "/bin/bash"
        # groups = "wheel audio video"
    };

    # Packages — user-specific (installed via Astral)
    # Packages: {
    #     firefox
    #     thunderbird
    # };

    Config: {
        # Dotfiles — symlink from /etc/astral/env/dotfiles/<user>/
        Dotfiles: {
            # "/home/" + username + R"(/.bashrc" = "bashrc"
        };
        Symlinks: {
            # "/home/" + username + R"(/.gitconfig" = "/etc/astral/env/dotfiles/" + username + R"(/gitconfig"
        };
    };

    Vars: {
        # EDITOR = "vim"
        # BROWSER = "firefox"
    };
};
)";

    std::ofstream f(user_path);
    f << content;

    // Set ownership to the user and permissions 664
    // Get uid/gid for the user
    auto result = util::run("id", {username});
    if (result.exit_code == 0) {
        // Parse uid/gid from id command output
        std::string output = result.stdout_output;
        size_t uid_pos = output.find("uid=");
        size_t gid_pos = output.find("gid=");
        if (uid_pos != std::string::npos && gid_pos != std::string::npos) {
            std::string uid_str = output.substr(uid_pos + 4);
            std::string gid_str = output.substr(gid_pos + 4);

            // Extract numbers
            uid_str = uid_str.substr(0, uid_str.find('('));
            gid_str = gid_str.substr(0, gid_str.find('('));

            // Change ownership
            auto chown_result = util::run("chown", {username + ":" + username, user_path.string()});
            (void)chown_result;  // Ignore errors - might not have permission
        }
    }

    // Set permissions: rw-rw-r-- (664)
    std::filesystem::permissions(user_path,
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_read |
        std::filesystem::perms::group_write |
        std::filesystem::perms::others_read);

    std::cout << "Created " << user_path << "\n";

    // Also create user's dotfiles directory
    auto dotfiles_user_dir = env_dir / "dotfiles" / username;
    if (!std::filesystem::exists(dotfiles_user_dir)) {
        std::filesystem::create_directories(dotfiles_user_dir);
        // Try to set ownership
        auto chown_result = util::run("chown", {username + ":" + username, dotfiles_user_dir.string()});
        (void)chown_result;
        std::filesystem::permissions(dotfiles_user_dir,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_all |
            std::filesystem::perms::others_exec |
            std::filesystem::perms::others_read);
        std::cout << "Created " << dotfiles_user_dir << "\n";
    }

    std::cout << "\nUser configuration created for '" << username << "'.\n";
    std::cout << "Edit " << user_path << " to add packages, dotfiles, and variables.\n";

    return 0;
}

int cmd_system_check(const std::vector<std::string>&) {
    auto env_dir = std::filesystem::path("/etc/astral/env");

    if (!std::filesystem::exists(env_dir)) {
        std::cout << "No env directory found.\n";
        return 0;
    }

    int errors = 0;

    for (const auto& entry : std::filesystem::directory_iterator(env_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".stars") {
            continue;
        }

        try {
            // FIX: Actually parse the file to validate it, not just check if it opens
            auto filename = entry.path().filename().string();

            if (filename == "env.stars") {
                // Try to parse as system config
                auto config = astral_sys::parse_system_config(entry.path());
                std::cout << "✓ " << filename << " — valid\n";
            } else {
                // Try to parse as user config
                auto config = astral_sys::parse_user_config(entry.path());
                std::cout << "✓ " << filename << " — valid\n";
            }
        } catch (const std::exception& e) {
            std::cout << "✗ " << entry.path().filename().string()
                      << " — ERROR: " << e.what() << "\n";
            errors++;
        }
    }

    if (errors > 0) {
        std::cout << "\n" << errors << " error(s) found.\n";
        return 1;
    }

    std::cout << "\nAll config files are valid.\n";
    return 0;
}

int cmd_snapd_start(const std::vector<std::string>&) {
    // Check if already running
    auto result = util::run("pidof", {"astral-env-snapd"});
    if (result.exit_code == 0 && !result.stdout_output.empty()) {
        std::cerr << "Error: astral-env-snapd is already running (PID: " << result.stdout_output << ")\n";
        return 1;
    }

    // Fork and run the daemon
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: fork failed\n";
        return 1;
    }
    if (pid == 0) {
        // Child process: run the daemon
        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Open /dev/null for stdin
        open("/dev/null", O_RDONLY);
        // Redirect stdout and stderr to a log file
        int log_fd = open("/var/log/astral-env-snapd.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        execlp("astral-env-snapd", "astral-env-snapd", nullptr);
        _exit(1);
    }

    // Parent process
    std::cout << "Started astral-env-snapd (PID: " << pid << ")\n";
    return 0;
}

int cmd_snapd_stop(const std::vector<std::string>&) {
    auto result = util::run("pidof", {"astral-env-snapd"});
    if (result.exit_code != 0 || result.stdout_output.empty()) {
        std::cerr << "Error: astral-env-snapd is not running\n";
        return 1;
    }

    // Extract PID
    std::string pid_str = result.stdout_output;
    // Take first PID if multiple
    size_t space = pid_str.find(' ');
    if (space != std::string::npos) {
        pid_str = pid_str.substr(0, space);
    }

    // Send SIGTERM
    int pid = std::stoi(pid_str);
    if (kill(pid, SIGTERM) != 0) {
        std::cerr << "Error: failed to stop daemon\n";
        return 1;
    }

    std::cout << "Stopped astral-env-snapd (PID: " << pid << ")\n";
    return 0;
}

int cmd_snapd_restart(const std::vector<std::string>&) {
    cmd_snapd_stop({});
    return cmd_snapd_start({});
}

int cmd_snapd_status(const std::vector<std::string>&) {
    auto result = util::run("pidof", {"astral-env-snapd"});
    if (result.exit_code == 0 && !result.stdout_output.empty()) {
        std::cout << "astral-env-snapd is running (PID: " << result.stdout_output << ")\n";
    } else {
        std::cout << "astral-env-snapd is not running\n";
    }
    return 0;
}

int cmd_system_sync_index(const std::vector<std::string>&) {
    auto env_path = std::filesystem::path("/etc/astral/env/env.stars");
    auto cache_dir = std::filesystem::path("/var/lib/astral-env/index");

    env::NodeMap root;
    if (std::filesystem::exists(env_path)) {
        try {
            root = env::parse_file(env_path);
        } catch (const std::exception& e) {
            std::cerr << "warning: could not parse env.stars: " << e.what() << "\n";
        }
    }

    auto repos = repo::parse_repos(root);
    if (repos.empty()) {
        std::cout << "No repositories configured.\n";
        return 0;
    }

    repo::sync_indexes(repos, cache_dir);
    return 0;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Check if we are the snapd binary
    std::string prog_name = argv[0];
    size_t pos = prog_name.rfind('/');
    if (pos != std::string::npos) {
        prog_name = prog_name.substr(pos + 1);
    }

    if (prog_name == "astral-env-snapd") {
        // Run the daemon directly
        run_daemon();
        return 0;
    }

    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    // Check for global flags FIRST (they can come before or after command)
    std::string cmd;
    std::vector<std::string> args;

    // First pass: check if first arg is a global flag
    if (argv[1][0] == '-' && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
                                 strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0)) {
        // Handle global-only flags
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // First arg is the command
    cmd = argv[1];

    // Parse remaining arguments
    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);

        // Also check remaining args for global flags
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // Load config for enabled check
    auto cfg = config::load();

    // Commands that don't require enabled check
    if (cmd == "init") {
        return cmd_init(args);
    }

    if (!cfg.env_enabled) {
        std::cerr << "ERROR: astral-env is not enabled.\n";
        std::cerr << "       Set 'astral-env = \"enabled\"' in $AST.core in /etc/astral/astral.stars\n";
        return 1;
    }

    // Dispatch commands
    if (cmd == "lock") {
        return cmd_lock(args);
    } else if (cmd == "build") {
        return cmd_build(args);
    } else if (cmd == "shell") {
        return cmd_shell(args);
    } else if (cmd == "run") {
        return cmd_run(args);
    } else if (cmd == "status") {
        return cmd_status(args);
    } else if (cmd == "gc") {
        return cmd_gc(args);
    } else if (cmd == "snap") {
        if (args.empty()) {
            std::cerr << "Usage: astral-env snap <path>  |  snap list [path]  |  snap restore <id>  |  snap prune\n";
            return 1;
        }
        const std::string& sub = args[0];
        if (sub == "list") {
            return cmd_snap_list({args.begin() + 1, args.end()});
        } else if (sub == "restore") {
            return cmd_snap_restore({args.begin() + 1, args.end()});
        } else if (sub == "prune") {
            return cmd_snap_prune({args.begin() + 1, args.end()});
        } else if (sub.front() == '/' || sub.front() == '.' || std::filesystem::exists(sub)) {
            // Explicit path — snapshot it
            return cmd_snap(args);
        } else {
            std::cerr << "Unknown snap subcommand: " << sub << "\n";
            std::cerr << "Usage: astral-env snap <path>  |  snap list [path]  |  snap restore <id>  |  snap prune\n";
            return 1;
        }
    } else if (cmd == "store") {
        if (args.empty()) {
            std::cerr << "Error: specify store subcommand\n";
            return 1;
        }
        if (args[0] == "list") {
            return cmd_store_list({args.begin() + 1, args.end()});
        } else if (args[0] == "size") {
            return cmd_store_size({args.begin() + 1, args.end()});
        }
    } else if (cmd == "system") {
        if (args.empty()) {
            std::cerr << "Error: specify system subcommand\n";
            return 1;
        }

        // system init doesn't require enabled flag
        if (args[0] == "init") {
            return cmd_system_init({args.begin() + 1, args.end()});
        }
        // system init-user doesn't require enabled flag (creates user config file)
        if (args[0] == "init-user") {
            return cmd_system_init_user({args.begin() + 1, args.end()});
        }
        // system hw-init — detect hardware, write /etc/astral/env/hw.stars
        if (args[0] == "hw-init") {
            return cmd_system_hw_init({args.begin() + 1, args.end()});
        }

        // Check system enabled for other commands
        if (!cfg.system_enabled) {
            std::cerr << "ERROR: astral-env-system is not enabled.\n";
            std::cerr << "       Set 'astral-env-system = \"enabled\"' in $AST.core in /etc/astral/astral.stars\n";
            return 1;
        }

        if (args[0] == "diff") {
            return cmd_system_diff({args.begin() + 1, args.end()});
        } else if (args[0] == "apply") {
            return cmd_system_apply({args.begin() + 1, args.end()});
        } else if (args[0] == "rollback") {
            return cmd_system_rollback({args.begin() + 1, args.end()});
        } else if (args[0] == "check") {
            return cmd_system_check({args.begin() + 1, args.end()});
        } else if (args[0] == "sync-index") {
            return cmd_system_sync_index({args.begin() + 1, args.end()});
        }
    } else if (cmd == "snapd") {
        if (args.empty()) {
            std::cerr << "Error: specify snapd subcommand\n";
            return 1;
        }
        if (args[0] == "start") return cmd_snapd_start({args.begin() + 1, args.end()});
        else if (args[0] == "stop") return cmd_snapd_stop({args.begin() + 1, args.end()});
        else if (args[0] == "restart") return cmd_snapd_restart({args.begin() + 1, args.end()});
        else if (args[0] == "status") return cmd_snapd_status({args.begin() + 1, args.end()});
        else {
            std::cerr << "Unknown snapd subcommand: " << args[0] << "\n";
            return 1;
        }
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_help(argv[0]);
    return 1;
}
