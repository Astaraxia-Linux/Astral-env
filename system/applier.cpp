#include "system/applier.hpp"
#include "util/process.hpp"
#include "util/file.hpp"
#include "util/tempdir.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace astral_sys {

namespace {

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H:%M");
    return ss.str();
}

// Delegate service management to astral — it handles init system detection internally
util::ProcessResult service_action(const std::string& action, const std::string& name) {
    return util::run("astral", {action, name});
}


void save_snapshot(const std::vector<DiffEntry>& pending_diffs) {
    auto snapshot_dir = std::filesystem::path("/astral-env/snapshots");
    std::filesystem::create_directories(snapshot_dir);

    auto timestamp = current_timestamp();
    auto snapshot_path = snapshot_dir / (timestamp + ".json");

    // Capture current state of everything that is about to be changed
    std::ofstream f(snapshot_path);
    f << "{\n";
    f << "  \"timestamp\": \"" << timestamp << "\",\n";

    // --- services ---
    f << "  \"services\": [\n";
    bool first_svc = true;
    for (const auto& d : pending_diffs) {
        if (d.type != "service") continue;
        if (!first_svc) f << ",\n";
        first_svc = false;
        f << "    {\"name\": \"" << d.name << "\", \"state\": \"" << d.current << "\"}";
    }
    f << "\n  ],\n";

    // --- dotfiles: record symlink targets that exist right now ---
    f << "  \"dotfiles\": [\n";
    bool first_dot = true;
    for (const auto& d : pending_diffs) {
        if (d.type != "dotfile") continue;
        auto dest = std::filesystem::path(d.name);
        std::string existing_target;
        bool existed = false;
        if (std::filesystem::is_symlink(dest)) {
            existed = true;
            existing_target = std::filesystem::read_symlink(dest).string();
        } else if (std::filesystem::exists(dest)) {
            existed = true;
            existing_target = ""; // regular file, not a symlink
        }
        if (!first_dot) f << ",\n";
        first_dot = false;
        f << "    {\"dest\": \"" << d.name << "\""
          << ", \"existed\": " << (existed ? "true" : "false")
          << ", \"was_symlink\": " << (std::filesystem::is_symlink(dest) ? "true" : "false")
          << ", \"target\": \"" << existing_target << "\"}";
    }
    f << "\n  ],\n";

    // --- system settings ---
    f << "  \"hostname\": \"";
    {
        auto r = util::run("hostname", {});
        std::string h = r.stdout_output;
        if (!h.empty() && h.back() == '\n') h.pop_back();
        f << h;
    }
    f << "\",\n";

    f << "  \"timezone\": \"";
    {
        auto r = util::run("timedatectl", {"show", "-p", "Timezone", "--value"});
        std::string tz = r.stdout_output;
        if (!tz.empty() && tz.back() == '\n') tz.pop_back();
        f << tz;
    }
    f << "\"\n";

    f << "}\n";
}

bool confirm_prompt(const std::string& message, bool yes) {
    if (yes) return true;
    
    std::cout << message << " [y/N] ";
    std::string response;
    std::getline(std::cin, response);
    
    return response == "y" || response == "Y";
}

} // anonymous namespace

int apply_diff(
    const std::vector<DiffEntry>& diffs,
    bool dry_run,
    bool,
    bool yes
) {
    int applied = 0;
    
    if (diffs.empty()) {
        std::cout << "No changes to apply.\n";
        return 0;
    }
    
    // Save snapshot before applying (captures current state of everything about to change)
    if (!dry_run) {
        save_snapshot(diffs);
    }
    
    // --- Parallel install: collect all packages, invoke astral once ---
    std::vector<std::string> to_install;
    for (const auto& d : diffs) {
        if (d.action == DiffAction::Install) {
            if (dry_run) {
                std::cout << "Install package " << d.name << "\n";
            } else {
                to_install.push_back(d.name);
            }
        }
    }

    if (!dry_run && !to_install.empty()) {
        if (to_install.size() == 1) {
            // Single package — no parallel overhead needed
            util::run("astral", {"-y", "-S", to_install[0]});
            auto verify = util::run("bash", {"-c",
                "find /var/lib/astral/db -maxdepth 3 -name version -path '*/"
                + to_install[0] + "/version' | grep -q ."});
            if (verify.exit_code == 0) {
                std::cout << "Installed " << to_install[0] << "\n";
                applied++;
            } else {
                std::cerr << "Failed to install " << to_install[0] << "\n";
            }
        } else {
            // Multiple packages — use astral --parallel-build
            std::vector<std::string> args = {"--parallel-build"};
            args.insert(args.end(), to_install.begin(), to_install.end());
            util::run("astral", args);

            for (const auto& pkg : to_install) {
                auto verify = util::run("bash", {"-c",
                    "find /var/lib/astral/db -maxdepth 3 -name version -path '*/"
                    + pkg + "/version' | grep -q ."});
                if (verify.exit_code == 0) {
                    std::cout << "Installed " << pkg << "\n";
                    applied++;
                } else {
                    std::cerr << "Failed to install " << pkg << "\n";
                }
            }
        }
    }

    // Handle all non-install diffs
    for (const auto& d : diffs) {
        if (d.action == DiffAction::Install) continue;

        if (dry_run) {
            std::cout << "Would: ";
        }

        switch (d.action) {
                
            case DiffAction::Enable:
                if (dry_run) {
                    std::cout << "Enable service " << d.name << "\n";
                } else {
                    auto result = service_action("enable", d.name);
                    if (result.exit_code == 0) {
                        std::cout << "Enabled " << d.name << "\n";
                        applied++;
                    }
                }
                break;
                
            case DiffAction::Disable:
                if (dry_run) {
                    std::cout << "Disable service " << d.name << "\n";
                } else {
                    auto result = service_action("disable", d.name);
                    if (result.exit_code == 0) {
                        std::cout << "Disabled " << d.name << "\n";
                        applied++;
                    }
                }
                break;
                
            case DiffAction::Change:
                if (d.name == "hostname") {
                    if (dry_run) {
                        std::cout << "Change hostname to " << d.target << "\n";
                    } else {
                        auto result = util::run("hostnamectl", {"set-hostname", d.target});
                        if (result.exit_code == 0) {
                            std::cout << "Hostname changed to " << d.target << "\n";
                            applied++;
                        }
                    }
                } else if (d.name == "timezone") {
                    if (dry_run) {
                        std::cout << "Change timezone to " << d.target << "\n";
                    } else {
                        auto result = util::run("timedatectl", {"set-timezone", d.target});
                        if (result.exit_code == 0) {
                            std::cout << "Timezone changed to " << d.target << "\n";
                            applied++;
                        }
                    }
                }
                break;
                
            case DiffAction::Symlink:
                if (dry_run) {
                    std::cout << "Create symlink " << d.name << " -> " << d.target << "\n";
                } else {
                    // Create parent directories if needed
                    auto dest_path = std::filesystem::path(d.name);
                    std::filesystem::create_directories(dest_path.parent_path());
                    // Set directory permissions: rwxrwxr-x (775)
                    std::filesystem::permissions(dest_path.parent_path(), 
                        std::filesystem::perms::owner_all | 
                        std::filesystem::perms::group_all | 
                        std::filesystem::perms::others_exec |
                        std::filesystem::perms::others_read);
                    
                    // Remove existing file if it's not a symlink
                    if (std::filesystem::exists(dest_path) && !std::filesystem::is_symlink(dest_path)) {
                        if (!confirm_prompt("Remove existing file " + dest_path.string() + "?", yes)) {
                            std::cout << "Skipped " << d.name << "\n";
                            continue;
                        }
                        std::filesystem::remove(dest_path);
                    }
                    
                    std::filesystem::create_symlink(d.target, dest_path);
                    std::cout << "Linked " << d.name << "\n";
                    applied++;
                }
                break;
                
            default:
                std::cout << "Skipped " << d.name << " (action not implemented)\n";
                break;
        }
    }
    
    return applied;
}

bool rollback(const std::string& snapshot_id) {
    auto snapshot_dir = std::filesystem::path("/astral-env/snapshots");

    if (!std::filesystem::exists(snapshot_dir)) {
        std::cerr << "No snapshots found.\n";
        return false;
    }

    // Find the snapshot file
    std::filesystem::path snapshot_path;
    if (snapshot_id.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(snapshot_dir)) {
            if (entry.is_regular_file()) {
                if (snapshot_path.empty() || entry.path().filename() > snapshot_path.filename()) {
                    snapshot_path = entry.path();
                }
            }
        }
    } else {
        snapshot_path = snapshot_dir / (snapshot_id + ".json");
    }

    if (!std::filesystem::exists(snapshot_path)) {
        std::cerr << "Snapshot not found: " << snapshot_path.string() << "\n";
        return false;
    }

    std::string content = util::read_file(snapshot_path);
    int restored = 0;
    int failed = 0;

    // --- Restore hostname ---
    {
        auto pos = content.find("\"hostname\":");
        if (pos != std::string::npos) {
            auto q1 = content.find('"', pos + 11);
            auto q2 = content.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string saved_host = content.substr(q1 + 1, q2 - q1 - 1);
                if (!saved_host.empty()) {
                    auto r = util::run("hostnamectl", {"set-hostname", saved_host});
                    if (r.exit_code == 0) {
                        std::cout << "  Restored hostname -> " << saved_host << "\n";
                        restored++;
                    } else {
                        std::cerr << "  Failed to restore hostname: " << r.stderr_output << "\n";
                        failed++;
                    }
                }
            }
        }
    }

    // --- Restore timezone ---
    {
        auto pos = content.find("\"timezone\":");
        if (pos != std::string::npos) {
            auto q1 = content.find('"', pos + 11);
            auto q2 = content.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string saved_tz = content.substr(q1 + 1, q2 - q1 - 1);
                if (!saved_tz.empty()) {
                    auto r = util::run("timedatectl", {"set-timezone", saved_tz});
                    if (r.exit_code == 0) {
                        std::cout << "  Restored timezone -> " << saved_tz << "\n";
                        restored++;
                    } else {
                        std::cerr << "  Failed to restore timezone: " << r.stderr_output << "\n";
                        failed++;
                    }
                }
            }
        }
    }

    // --- Restore services ---
    {
        auto services_start = content.find("\"services\":");
        auto services_end   = content.find(']', services_start);
        if (services_start != std::string::npos && services_end != std::string::npos) {
            std::string services_block = content.substr(services_start, services_end - services_start);
            size_t cursor = 0;
            while (true) {
                auto name_pos = services_block.find("\"name\":", cursor);
                if (name_pos == std::string::npos) break;
                auto q1 = services_block.find('"', name_pos + 7);
                auto q2 = services_block.find('"', q1 + 1);
                std::string svc_name = services_block.substr(q1 + 1, q2 - q1 - 1);

                auto state_pos = services_block.find("\"state\":", q2);
                auto sq1 = services_block.find('"', state_pos + 8);
                auto sq2 = services_block.find('"', sq1 + 1);
                std::string svc_state = services_block.substr(sq1 + 1, sq2 - sq1 - 1);

                cursor = sq2 + 1;

                if (!svc_name.empty() && !svc_state.empty()) {
                    std::string action = (svc_state == "enabled") ? "enable" : "disable";
                    auto r = service_action(action, svc_name);
                    if (r.exit_code == 0) {
                        std::cout << "  Restored service " << svc_name << " -> " << svc_state << "\n";
                        restored++;
                    } else {
                        std::cerr << "  Failed to restore service " << svc_name << ": " << r.stderr_output << "\n";
                        failed++;
                    }
                }
            }
        }
    }

    // --- Restore dotfiles (symlinks) ---
    {
        auto dotfiles_start = content.find("\"dotfiles\":");
        auto dotfiles_end   = content.find(']', dotfiles_start);
        if (dotfiles_start != std::string::npos && dotfiles_end != std::string::npos) {
            std::string dotfiles_block = content.substr(dotfiles_start, dotfiles_end - dotfiles_start);
            size_t cursor = 0;
            while (true) {
                auto dest_pos = dotfiles_block.find("\"dest\":", cursor);
                if (dest_pos == std::string::npos) break;
                auto q1 = dotfiles_block.find('"', dest_pos + 7);
                auto q2 = dotfiles_block.find('"', q1 + 1);
                std::string dest_str = dotfiles_block.substr(q1 + 1, q2 - q1 - 1);

                bool existed = dotfiles_block.find("\"existed\": true", q2) != std::string::npos
                            && dotfiles_block.find("\"existed\": true", q2) < dotfiles_block.find("\"dest\":", q2 + 1);
                bool was_symlink = dotfiles_block.find("\"was_symlink\": true", q2) != std::string::npos
                                && dotfiles_block.find("\"was_symlink\": true", q2) < dotfiles_block.find("\"dest\":", q2 + 1);

                auto target_pos = dotfiles_block.find("\"target\":", q2);
                auto tq1 = dotfiles_block.find('"', target_pos + 9);
                auto tq2 = dotfiles_block.find('"', tq1 + 1);
                std::string target_str = dotfiles_block.substr(tq1 + 1, tq2 - tq1 - 1);

                cursor = tq2 + 1;

                auto dest_path = std::filesystem::path(dest_str);
                try {
                    if (!existed) {
                        // Symlink didn't exist before — remove it if it's there now
                        if (std::filesystem::is_symlink(dest_path) || std::filesystem::exists(dest_path)) {
                            std::filesystem::remove(dest_path);
                            std::cout << "  Removed symlink " << dest_str << "\n";
                            restored++;
                        }
                    } else if (was_symlink && !target_str.empty()) {
                        // Was a symlink to something else — restore it
                        if (std::filesystem::is_symlink(dest_path) || std::filesystem::exists(dest_path)) {
                            std::filesystem::remove(dest_path);
                        }
                        std::filesystem::create_symlink(target_str, dest_path);
                        std::cout << "  Restored symlink " << dest_str << " -> " << target_str << "\n";
                        restored++;
                    }
                    // If it was a regular file (not a symlink), we can't restore it — log a warning
                    else if (existed && !was_symlink) {
                        std::cerr << "  Warning: " << dest_str << " was a regular file before apply — cannot restore automatically\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "  Failed to restore dotfile " << dest_str << ": " << e.what() << "\n";
                    failed++;
                }
            }
        }
    }

    std::cout << "\nRollback complete: " << restored << " restored";
    if (failed > 0) std::cout << ", " << failed << " failed";
    std::cout << "\n";

    return failed == 0;
}

std::vector<std::string> list_snapshots() {
    std::vector<std::string> snapshots;
    
    auto snapshot_dir = std::filesystem::path("/astral-env/snapshots");
    
    if (!std::filesystem::exists(snapshot_dir)) {
        return snapshots;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(snapshot_dir)) {
        if (entry.is_regular_file()) {
            snapshots.push_back(entry.path().stem().string());
        }
    }
    
    std::sort(snapshots.begin(), snapshots.end(), std::greater<std::string>());
    
    return snapshots;
}

bool rollback_implemented() {
    return true;
}

} // namespace astral_sys