#include "snap/snap.hpp"
#include "env/parser.hpp"
#include "env/node.hpp"
#include "util/file.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <signal.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace env;

struct SnapConfig {
    std::filesystem::path path;
    bool   is_autosave      = false;
    int    interval_seconds = 3600; // 1H default
    int    debounce_seconds = 5;    // default autosave debounce

    std::chrono::system_clock::time_point last_snapshot;
    std::chrono::system_clock::time_point last_change;

    std::mutex              mu;
    std::condition_variable cv;
    bool                    triggered = false;

    SnapConfig() = default;
    SnapConfig(const SnapConfig&) = delete;
    SnapConfig& operator=(const SnapConfig&) = delete;
    SnapConfig(SnapConfig&&) = default;
};

std::atomic<bool> g_stop{false};

void signal_handler(int) { g_stop = true; }

int parse_interval(const std::string& s) {
    if (s == "autosave") return 0;
    std::istringstream ss(s);
    int v; std::string u;
    if (!(ss >> v >> u)) return -1;
    if (u == "S") return v;
    if (u == "M") return v * 60;
    if (u == "H") return v * 3600;
    if (u == "D") return v * 86400;
    return -1;
}

void take_snapshot(const SnapConfig& cfg, const std::string& reason) {
    try {
        auto r = snap::create(cfg.path, snap::SNAP_STORE, snap::SNAP_INDEX, reason);
        std::cout << "[snapd] " << r.snapshot_id << " " << cfg.path
                  << " (" << reason << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "[snapd] failed " << cfg.path << ": " << e.what() << "\n";
    }
}

std::vector<std::unique_ptr<SnapConfig>> load_configs(
    const std::filesystem::path& env_dir) {

    std::vector<std::unique_ptr<SnapConfig>> out;
    if (!std::filesystem::exists(env_dir)) return out;

    std::string default_interval = "1 H";

    for (const auto& entry : std::filesystem::directory_iterator(env_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".stars") continue;
        try {
            NodeMap root = parse_file(entry.path());
            Node r(root);
            auto snap_node = r.get("Snap");
            if (!snap_node || !(*snap_node)->is_map()) continue;
            const Node& sn = **snap_node;

            if (auto p = sn.get("default_interval"); p && (*p)->is_str())
                default_interval = (*p)->str();

            auto path_node = sn.get("path");
            if (!path_node || !(*path_node)->is_map()) continue;

            for (const auto& [pkey, pval] : (*path_node)->map()) {
                if (pkey.rfind("__item_", 0) == 0) {
                    if (!pval.is_str()) continue;
                    auto cfg = std::make_unique<SnapConfig>();
                    cfg->path = pval.str();
                    int secs = parse_interval(default_interval);
                    cfg->is_autosave      = (secs == 0);
                    cfg->interval_seconds = (secs > 0) ? secs : 3600;
                    cfg->last_snapshot    = std::chrono::system_clock::now();
                    out.push_back(std::move(cfg));
                } else {
                    auto cfg = std::make_unique<SnapConfig>();
                    cfg->path = pkey;
                    std::string interval = default_interval;
                    if (pval.is_map()) {
                        if (auto i = pval.get("interval"); i && (*i)->is_str())
                            interval = (*i)->str();
                        if (auto d = pval.get("autosave_debounce"); d && (*d)->is_str()) {
                            int dsecs = parse_interval((*d)->str());
                            if (dsecs > 0) cfg->debounce_seconds = dsecs;
                        }
                    }
                    int secs = parse_interval(interval);
                    cfg->is_autosave      = (secs == 0);
                    cfg->interval_seconds = (secs > 0) ? secs : 3600;
                    cfg->last_snapshot    = std::chrono::system_clock::now();
                    out.push_back(std::move(cfg));
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[snapd] parse error " << entry.path() << ": " << e.what() << "\n";
        }
    }
    return out;
}

void debounce_worker(SnapConfig* cfg) {
    std::unique_lock<std::mutex> lock(cfg->mu);
    while (!g_stop) {
        cfg->cv.wait_for(lock, std::chrono::seconds(cfg->debounce_seconds),
            [&] { return cfg->triggered || g_stop.load(); });
        if (g_stop) break;
        if (!cfg->triggered) continue;

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - cfg->last_change).count();
        if (elapsed >= cfg->debounce_seconds) {
            cfg->triggered = false;
            lock.unlock();
            take_snapshot(*cfg, "autosave");
            lock.lock();
            cfg->last_snapshot = std::chrono::system_clock::now();
        }
    }
}

void watch_autosave(const std::vector<std::unique_ptr<SnapConfig>>& cfgs, int ifd) {
    std::map<int, SnapConfig*> wd_map;
    for (const auto& c : cfgs) {
        if (!c->is_autosave) continue;
        int wd = inotify_add_watch(ifd, c->path.c_str(),
            IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
        if (wd >= 0) wd_map[wd] = c.get();
    }

    char buf[4096] __attribute__((aligned(__alignof__(inotify_event))));
    while (!g_stop) {
        ssize_t n = read(ifd, buf, sizeof(buf));
        if (n < 0) { if (errno == EINTR) continue; break; }
        for (char* p = buf; p < buf + n; ) {
            auto* ev = reinterpret_cast<const inotify_event*>(p);
            auto it = wd_map.find(ev->wd);
            if (it != wd_map.end()) {
                SnapConfig* cfg = it->second;
                std::lock_guard<std::mutex> lk(cfg->mu);
                cfg->last_change = std::chrono::system_clock::now();
                cfg->triggered   = true;
                cfg->cv.notify_one();
            }
            p += sizeof(inotify_event) + ev->len;
        }
    }
    for (const auto& kv : wd_map) inotify_rm_watch(ifd, kv.first);
}

void daemon_main() {
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    auto cfgs = load_configs("/etc/astral/env");
    if (cfgs.empty()) { std::cout << "[snapd] no snapshot paths configured\n"; return; }
    std::cout << "[snapd] watching " << cfgs.size() << " path(s)\n";

    std::vector<std::thread> debounce_threads;
    for (const auto& c : cfgs)
        if (c->is_autosave)
            debounce_threads.emplace_back(debounce_worker, c.get());

    int ifd = inotify_init1(IN_NONBLOCK);
    if (ifd < 0) { std::cerr << "[snapd] inotify_init failed\n"; return; }

    std::thread watcher(watch_autosave, std::cref(cfgs), ifd);

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        if (g_stop) break;
        auto now = std::chrono::system_clock::now();
        for (const auto& c : cfgs) {
            if (c->is_autosave) continue;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - c->last_snapshot).count();
            if (elapsed >= c->interval_seconds) {
                take_snapshot(*c, "scheduled");
                c->last_snapshot = now;
            }
        }
    }

    g_stop = true;
    for (const auto& c : cfgs) {
        std::lock_guard<std::mutex> lk(c->mu);
        c->triggered = true;
        c->cv.notify_all();
    }
    for (auto& t : debounce_threads) if (t.joinable()) t.join();
    if (watcher.joinable()) watcher.join();
    close(ifd);
    std::cout << "[snapd] stopped\n";
}

} // anonymous namespace

extern "C" void run_daemon() { daemon_main(); }
