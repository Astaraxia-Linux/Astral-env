#include "snap/snap.hpp"
#include "system/system_conf.hpp"
#include "util/file.hpp"
#include "util/process.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <limits.h>
#include <atomic>
#include <memory>

namespace {

// Structure to hold snapshot configuration for a path
struct SnapConfig {
    std::filesystem::path path;
    std::string interval; // e.g., "1 H", "30 M", "autosave"
    int interval_seconds = 0; // 0 for autosave
    std::chrono::system_clock::time_point last_snapshot;
    bool is_autosave = false;
    // For autosave debouncing
    std::chrono::system_clock::time_point last_change;
    std::thread debounce_thread;
    std::mutex debounce_mutex;
    std::condition_variable debounce_cv;
    bool debounce_trigger = false;

    // Delete copy constructor and copy assignment
    SnapConfig(const SnapConfig&) = delete;
    SnapConfig& operator=(const SnapConfig&) = delete;

    // Allow move constructor and move assignment
    SnapConfig(SnapConfig&&) = default;
    SnapConfig& operator=(SnapConfig&&) = default;

    // Default constructor (needed for make_unique)
    SnapConfig() {}
};

// Global flag to indicate daemon should stop
std::atomic<bool> g_stop_daemon(false);

// Signal handler
void signal_handler(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        g_stop_daemon = true;
    }
}

// Parse interval string like "1 H" into seconds
// Returns 0 for autosave, -1 on error
int parse_interval_to_seconds(const std::string& interval) {
    if (interval == "autosave") {
        return 0;
    }
    
    std::istringstream iss(interval);
    int value;
    std::string unit;
    if (!(iss >> value >> unit)) {
        return -1;
    }
    
    if (unit == "S") {
        return value;
    } else if (unit == "M") {
        return value * 60;
    } else if (unit == "H") {
        return value * 3600;
    } else if (unit == "D") {
        return value * 86400;
    }
    
    return -1;
}

// Load snapshot configuration from all .stars files in /etc/astral/env/
std::vector<std::unique_ptr<SnapConfig>> load_snap_config(const std::filesystem::path& env_dir) {
    std::vector<std::unique_ptr<SnapConfig>> configs;
    
    if (!std::filesystem::exists(env_dir)) {
        return configs;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(env_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".stars") continue;
        
        // Skip env.stars? Actually we want to include it for system snapshots
        // But we'll process all .stars files
        
        std::string content = util::read_file(entry.path());
        std::istringstream iss(content);
        std::string line;
        
        bool in_snap = false;
        bool in_path = false;
        std::string current_path;
        std::string current_interval;
        
        while (std::getline(iss, line)) {
            // Trim whitespace
            auto start = line.find_first_not_of(" \t");
            auto end = line.find_last_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start, end - start + 1);
            
            if (line.empty() || line[0] == '#') continue;
            
            if (line.find("$ENV.Snap") != std::string::npos) {
                in_snap = true;
                in_path = false;
                continue;
            }
            
            if (in_snap && line == "};") {
                in_snap = false;
                in_path = false;
                // If we have a pending path, add it
                if (!current_path.empty()) {
                    auto config = std::make_unique<SnapConfig>();
                    config->path = current_path;
                    config->interval = current_interval;
                    config->interval_seconds = parse_interval_to_seconds(current_interval);
                    config->is_autosave = (config->interval_seconds == 0);
                    config->last_snapshot = std::chrono::system_clock::now(); // Initialize to now
                    configs.push_back(std::move(config));
                    current_path.clear();
                    current_interval.clear();
                }
                continue;
            }
            
            if (in_snap && line.find("on_interval") != std::string::npos) {
                // We'll ignore on_interval for now, assuming it's true if section exists
                continue;
            }
            
            if (in_snap && line.find("default_interval") != std::string::npos) {
                // We'll handle default_interval later if needed
                continue;
            }
            
            if (in_snap && line.find("path:") != std::string::npos) {
                in_path = true;
                continue;
            }
            
            if (in_path) {
                // Check if this line ends the path block
                if (line == "};") {
                    in_path = false;
                    // Add the pending path
                    if (!current_path.empty()) {
                        auto config = std::make_unique<SnapConfig>();
                        config->path = current_path;
                        config->interval = current_interval;
                        config->interval_seconds = parse_interval_to_seconds(current_interval);
                        config->is_autosave = (config->interval_seconds == 0);
                        config->last_snapshot = std::chrono::system_clock::now();
                        configs.push_back(std::move(config));
                        current_path.clear();
                        current_interval.clear();
                    }
                    continue;
                }
                
                // Parse path line: either a bare path or path: { ... }
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    // This is a path with options
                    std::string path_str = line.substr(0, colon_pos);
                    // Trim quotes and whitespace
                    auto start = path_str.find_first_not_of(" \t\"");
                    auto end = path_str.find_last_not_of(" \t\"");
                    if (start != std::string::npos && end != std::string::npos) {
                        path_str = path_str.substr(start, end - start + 1);
                    }
                    current_path = path_str;
                    
                    // Look for interval in the same line or subsequent lines
                    // For simplicity, we'll assume interval is on the same line after the colon
                    std::string options = line.substr(colon_pos + 1);
                    // Trim
                    auto opt_start = options.find_first_not_of(" \t");
                    auto opt_end = options.find_last_not_of(" \t");
                    if (opt_start != std::string::npos && opt_end != std::string::npos) {
                        options = options.substr(opt_start, opt_end - opt_start + 1);
                    }
                    if (options.find("interval") != std::string::npos) {
                        // Extract interval value
                        size_t eq_pos = options.find('=');
                        if (eq_pos != std::string::npos) {
                            std::string interval_str = options.substr(eq_pos + 1);
                            // Trim quotes and whitespace
                            auto i_start = interval_str.find_first_not_of(" \t\"");
                            auto i_end = interval_str.find_last_not_of(" \t\"");
                            if (i_start != std::string::npos && i_end != std::string::npos) {
                                interval_str = interval_str.substr(i_start, i_end - i_start + 1);
                            }
                            current_interval = interval_str;
                        }
                    }
                } else {
                    // Bare path (uses default_interval)
                    // Trim quotes and whitespace
                    auto start = line.find_first_not_of(" \t\"");
                    auto end = line.find_last_not_of(" \t\"");
                    if (start != std::string::npos && end != std::string::npos) {
                        current_path = line.substr(start, end - start + 1);
                    }
                    // Interval will be set from default_interval later
                }
            }
        }
        
        // Handle case where file ends without closing brace (shouldn't happen but be safe)
        if (in_path && !current_path.empty()) {
            auto config = std::make_unique<SnapConfig>();
            config->path = current_path;
            config->interval = current_interval.empty() ? "1 H" : current_interval; // fallback
            config->interval_seconds = parse_interval_to_seconds(config->interval);
            config->is_autosave = (config->interval_seconds == 0);
            config->last_snapshot = std::chrono::system_clock::now();
            configs.push_back(std::move(config));
        }
    }
    
    return configs;
}

// Function to take a snapshot for a given config
void take_snapshot(const SnapConfig& config, const std::string& reason) {
    try {
        auto result = snap::create(
            config.path,
            snap::SNAP_STORE,
            snap::SNAP_INDEX,
            reason
        );
        
        std::cout << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) 
                  << "] Snapshot taken: " << result.snapshot_id 
                  << " for " << config.path 
                  << " (reason: " << reason << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) 
                  << "] Failed to snapshot " << config.path 
                  << ": " << e.what() << std::endl;
    }
}

// Debounce thread for autosave paths
void debounce_thread_func(SnapConfig* config) {
    std::unique_lock<std::mutex> lock(config->debounce_mutex);
    while (!g_stop_daemon.load()) {
        // Wait for trigger or timeout (5 seconds)
        if (config->debounce_cv.wait_for(lock, std::chrono::seconds(5), 
                                         [&] { return config->debounce_trigger || g_stop_daemon.load(); })) {
            if (g_stop_daemon.load()) break;
            
            // Check if 5 seconds have passed since last change
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - config->last_change).count();
            
            if (elapsed >= 5) {
                // Take snapshot
                take_snapshot(*config, "autosave");
                config->last_snapshot = now;
                config->debounce_trigger = false;
            }
        } else {
            // Timeout occurred (5 seconds of no changes)
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - config->last_change).count();
            
            if (elapsed >= 5) {
                // Take snapshot
                take_snapshot(*config, "autosave");
                config->last_snapshot = now;
                config->debounce_trigger = false;
            }
        }
    }
}

// Watch for changes on autosave paths using inotify
void watch_autosave_paths(const std::vector<std::unique_ptr<SnapConfig>>& configs, int fd) {
    std::map<int, SnapConfig*> wd_to_config;
    
    for (const auto& config_ptr : configs) {
        if (!config_ptr->is_autosave) continue;
        
        int wd = inotify_add_watch(fd, config_ptr->path.c_str(), 
                                   IN_MODIFY | IN_CREATE | IN_DELETE | 
                                   IN_MOVED_TO | IN_MOVED_FROM);
        if (wd >= 0) {
            wd_to_config[wd] = config_ptr.get();
            std::cout << "Watching autosave path: " << config_ptr->path << " (wd=" << wd << ")" << std::endl;
        } else {
            std::cerr << "Failed to watch autosave path: " << config_ptr->path << std::endl;
        }
    }
    
    // Event loop
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event* event;
    
    while (!g_stop_daemon.load()) {
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        for (char* ptr = buffer; ptr < buffer + len; 
             ptr += sizeof(struct inotify_event) + event->len) {
            event = reinterpret_cast<const struct inotify_event*>(ptr);
            
            auto it = wd_to_config.find(event->wd);
            if (it != wd_to_config.end()) {
                SnapConfig* config = it->second;
                {
                    std::lock_guard<std::mutex> lock(config->debounce_mutex);
                    config->last_change = std::chrono::system_clock::now();
                    config->debounce_trigger = true;
                    config->debounce_cv.notify_one();
                }
            }
        }
    }
    
    // Clean up watches
    for (const auto& pair : wd_to_config) {
        inotify_rm_watch(fd, pair.first);
    }
}

// Main daemon loop
void daemon_main() {
    // Set up signal handling
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    const std::filesystem::path env_dir = "/etc/astral/env";
    
    // Load initial configuration
    auto configs = load_snap_config(env_dir);
    
    if (configs.empty()) {
        std::cout << "No snapshot paths configured. Daemon will exit." << std::endl;
        return;
    }
    
    std::cout << "Loaded " << configs.size() << " snapshot paths." << std::endl;
    
    // Start debounce threads for autosave paths
    std::vector<std::thread> debounce_threads;
    for (const auto& config_ptr : configs) {
        if (config_ptr->is_autosave) {
            debounce_threads.emplace_back(debounce_thread_func, config_ptr.get());
        }
    }
    
    // Set up inotify for autosave paths
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to initialize inotify" << std::endl;
        return;
    }
    
    std::thread watcher_thread(watch_autosave_paths, std::cref(configs), fd);
    
    // Main loop: check every 60 seconds for scheduled snapshots
    while (!g_stop_daemon.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        if (g_stop_daemon.load()) break;
        
        auto now = std::chrono::system_clock::now();
        
        for (const auto& config_ptr : configs) {
            if (config_ptr->is_autosave) continue;
            
            if (config_ptr->interval_seconds <= 0) continue;
            
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - config_ptr->last_snapshot).count();
            
            if (elapsed >= config_ptr->interval_seconds) {
                take_snapshot(*config_ptr, "scheduled");
                config_ptr->last_snapshot = now;
            }
        }
    }
    
    // Clean up
    g_stop_daemon.store(true);
    
    // Notify debounce threads
    for (const auto& config_ptr : configs) {
        if (config_ptr->is_autosave) {
            std::lock_guard<std::mutex> lock(config_ptr->debounce_mutex);
            config_ptr->debounce_trigger = true;
            config_ptr->debounce_cv.notify_one();
        }
    }
    
    // Join threads
    for (auto& t : debounce_threads) {
        if (t.joinable()) t.join();
    }
    if (watcher_thread.joinable()) watcher_thread.join();
    
    close(fd);
    
    std::cout << "Snapshot daemon stopped." << std::endl;
}

} // namespace

// This function is called from main.cpp when the binary is run as astral-env-snapd
extern "C" void run_daemon() {
    daemon_main();
}