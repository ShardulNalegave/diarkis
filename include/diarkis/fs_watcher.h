
#ifndef DIARKIS_FS_WATCHER_H
#define DIARKIS_FS_WATCHER_H

#include <string>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <sys/inotify.h>

#include "diarkis/events.h"

namespace fs {

    class Watcher {
    public:
        explicit Watcher(const std::string& watch_dir, events::EventHandler callback_);
        ~Watcher();

        bool start();
        void stop();
        bool isRunning() const { return running; };

        void ignoreNextEvent(const std::string& path);
        bool shouldIgnoreEvent(const std::string& path);

    private:
        void watchLoop();

        bool addWatch(const std::string& path);
        void removeWatch(int wd);
        void handleEvent(const struct inotify_event* event);

        std::string getPathFromWD(int wd) const;
        int getWDFromPath(const std::string& path) const;

        std::map<int, std::string> wd_to_path;
        std::map<std::string, int> path_to_wd;
        mutable std::mutex watch_map_mutex;

        events::EventHandler callback;
        std::mutex callback_mutex;

        int inotify_fd;
        std::string root_watch_dir;
        std::atomic<bool> running;
        std::thread watch_thread;

        struct MoveContext {
            std::string from_path;
            uint32_t cookie;
            bool is_dir;
            std::chrono::steady_clock::time_point time;
        };

        std::map<uint32_t, MoveContext> pending_moves;
        std::mutex move_mutex;

        std::set<std::string> ignored_paths;
        std::mutex ignore_mutex;
    };

};

#endif /* DIARKIS_FS_WATCHER_H*/
