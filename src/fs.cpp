
#include "diarkis/fs.h"

#include <cstring>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>

#include <spdlog/spdlog.h>

namespace fs {

static bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return false;
    return S_ISDIR(st.st_mode);
}

static void listSubdirs(const std::string& path, std::vector<std::string>& dirs) {
    if (!isDirectory(path)) return;

    DIR* dir = opendir(path.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string full_path = path + "/" + entry->d_name;
        if (isDirectory(full_path)) {
            dirs.push_back(full_path);
            listSubdirs(full_path, dirs);
        }
    }

    closedir(dir);
}

Watcher::Watcher(const std::string& watch_dir, EventCallback callback_)
    : root_watch_dir(watch_dir), running(false), callback(callback_), inotify_fd(-1) {
    //
}

Watcher::~Watcher() {
    stop();
}

bool Watcher::start() {
    if (running) {
        spdlog::error("Filesystem Watcher is already running");
        return false;
    }

    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        spdlog::error("Filesystem Watcher could not acquire a inotify FD");
        return false;
    }

    if (!addWatch(root_watch_dir)) {
        close(inotify_fd);
        inotify_fd = -1;
        return false;
    }

    running = true;
    watch_thread = std::thread(&Watcher::watchLoop, this);

    spdlog::info("Filesystem Watcher started listening on: {}", root_watch_dir);
    return true;
}

void Watcher::stop() {
    if (!running) return;

    running = false;
    if (watch_thread.joinable()) watch_thread.join();

    if (inotify_fd >= 0) {
        std::lock_guard<std::mutex> lock(watch_map_mutex);
        for (const auto& pair : wd_to_path) {
            inotify_rm_watch(inotify_fd, pair.first);
        }
        close(inotify_fd);
        inotify_fd = -1;
    }

    wd_to_path.clear();
    path_to_wd.clear();

    spdlog::info("Filesystem Watcher stopped");
}

bool Watcher::addWatch(const std::string& path) {
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | 
                    IN_MOVED_FROM | IN_MOVED_TO |
                    IN_DELETE_SELF | IN_MOVE_SELF;

    int wd = inotify_add_watch(inotify_fd, path.c_str(), mask);
    if (wd < 0) {
        spdlog::error("Failed to add watch for {}\n\t{}", path, strerror(errno));
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(watch_map_mutex);
        wd_to_path[wd] = path;
        path_to_wd[path] = wd;
    }

    if (isDirectory(path)) {
        std::vector<std::string> subdirs;
        listSubdirs(path, subdirs);
        
        for (const auto& subdir : subdirs) {
            addWatch(subdir);
        }
    }

    return true;
}

void Watcher::removeWatch(int wd) {
    std::lock_guard<std::mutex> lock(watch_map_mutex);
    
    auto it = wd_to_path.find(wd);
    if (it != wd_to_path.end()) {
        path_to_wd.erase(it->second);
        wd_to_path.erase(it);
        inotify_rm_watch(inotify_fd, wd);
    }
}

std::string Watcher::getPathFromWD(int wd) const {
    std::lock_guard<std::mutex> lock(watch_map_mutex);
    auto it = wd_to_path.find(wd);
    return (it != wd_to_path.end()) ? it->second : "";
}

int Watcher::getWDFromPath(const std::string& path) const {
    std::lock_guard<std::mutex> lock(watch_map_mutex);
    auto it = path_to_wd.find(path);
    return (it != path_to_wd.end()) ? it->second : -1;
}

void Watcher::watchLoop() {
    const size_t BUF_SIZE = 4096;
    char buffer[BUF_SIZE] __attribute__((aligned(__alignof__(struct inotify_event))));

    while (running) {
        ssize_t len = read(inotify_fd, buffer, BUF_SIZE);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            } else {
                spdlog::error("Filesystem Watcher: Error reading inotify events\n\t{}", strerror(errno));
                break;
            }
        }

        const struct inotify_event* event;
        for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = reinterpret_cast<const struct inotify_event*>(ptr);
            handleEvent(event);
        }
    }
}

void Watcher::handleEvent(const struct inotify_event* event) {
    if (event->len == 0) {
        return;
    }
    
    std::string dir_path = getPathFromWD(event->wd);
    if (dir_path.empty()) {
        return;
    }
    
    std::string full_path = dir_path + "/" + event->name;
    bool is_dir = (event->mask & IN_ISDIR) != 0;

    Event file_event;
    file_event.path = full_path;
    file_event.relative_path = full_path.substr(root_watch_dir.size());
    if (file_event.relative_path[0] == '/') {
        file_event.relative_path = file_event.relative_path.substr(1);
    }
    file_event.is_dir = is_dir;

    if (event->mask & IN_CREATE) {
        file_event.type = EventType::CREATED;
        
        if (is_dir) {
            addWatch(full_path);
        }
    } else if (event->mask & IN_DELETE) {
        file_event.type = EventType::DELETED;
        
        if (is_dir) {
            std::lock_guard<std::mutex> lock(watch_map_mutex);
            auto it = path_to_wd.find(full_path);
            if (it != path_to_wd.end()) {
                removeWatch(it->second);
            }
        }
    } else if (event->mask & IN_MODIFY) {
        file_event.type = EventType::MODIFIED;
    } else {
        return;
    }

    std::lock_guard<std::mutex> lock(callback_mutex);
    if (callback) {
        callback(file_event);
    }
}

};
