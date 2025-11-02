#include "diarkis/fs_watcher.h"

#include <poll.h>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

namespace fs {

static bool isDirectory(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

static bool readFileContents(const std::string& path, std::string& contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        spdlog::warn("Failed to open file for reading: {}", path);
        return false;
    }
    
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    const std::streamsize MAX_FILE_SIZE = 10 * 1024 * 1024;
    if (size > MAX_FILE_SIZE) {
        spdlog::warn("File too large to read into memory ({}MB): {}", size / (1024*1024), path);
        return false;
    }
    
    if (size == 0) {
        contents.clear();
        return true;
    }
    
    contents.resize(size);
    if (!file.read(&contents[0], size)) {
        spdlog::warn("Failed to read file contents: {}", path);
        return false;
    }
    
    return true;
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

Watcher::Watcher(const std::string& watch_dir, events::EventHandler callback_)
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

    struct pollfd pfd;
    pfd.fd = inotify_fd;
    pfd.events = POLLIN; // read events only

    while (running) {
        int poll_result = poll(&pfd, 1, 1000); // poll every 1 second

        if (poll_result < 0) {
            if (errno == EINTR) {
                // interrupted by signal, continue
                continue;
            }
            spdlog::error("Filesystem Watcher: poll() error\n\t{}", strerror(errno));
            break;
        }

        if (pfd.revents & POLLIN) {
            // data is available to read
            ssize_t len = read(inotify_fd, buffer, BUF_SIZE);
            
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // should not happen with poll, fallback
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

        // handle pending moves (more than 5 seconds)
        // pending means that the file was moved outside the observed directory tree, so we handle it as a delete
        {
            std::lock_guard<std::mutex> lock(move_mutex);
            auto now = std::chrono::steady_clock::now();
            for (auto it = pending_moves.begin(); it != pending_moves.end();) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.time).count();
                if (elapsed > 5) {
                    events::Event file_event;
                    file_event.type = events::EventType::DELETED;
                    file_event.path = it->second.from_path;
                    file_event.relative_path = it->second.from_path.substr(root_watch_dir.size());
                    if (file_event.relative_path[0] == '/') {
                        file_event.relative_path = file_event.relative_path.substr(1);
                    }
                    file_event.is_dir = it->second.is_dir;

                    {
                        std::lock_guard<std::mutex> cb_lock(callback_mutex);
                        if (callback) {
                            callback(file_event);
                        }
                    }
                    
                    it = pending_moves.erase(it);
                } else {
                    ++it;
                }
            }
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

    events::Event file_event;
    file_event.path = full_path;
    file_event.relative_path = full_path.substr(root_watch_dir.size());
    if (file_event.relative_path[0] == '/') {
        file_event.relative_path = file_event.relative_path.substr(1);
    }
    file_event.is_dir = is_dir;

    if (event->mask & IN_CREATE) {
        file_event.type = events::EventType::CREATED;
        if (!is_dir) {
            readFileContents(full_path, file_event.contents);
        }
        
        if (is_dir) {
            addWatch(full_path);
        }
    } else if (event->mask & IN_DELETE) {
        file_event.type = events::EventType::DELETED;
        
        if (is_dir) {
            std::lock_guard<std::mutex> lock(watch_map_mutex);
            auto it = path_to_wd.find(full_path);
            if (it != path_to_wd.end()) {
                removeWatch(it->second);
            }
        }
    } else if (event->mask & IN_MODIFY) {
        file_event.type = events::EventType::MODIFIED;
        if (!is_dir) {
            readFileContents(full_path, file_event.contents);
        }
    } else if (event->mask & IN_MOVED_FROM) {
        std::lock_guard<std::mutex> lock(move_mutex);
        pending_moves[event->cookie] = {
            full_path,
            event->cookie,
            is_dir,
            std::chrono::steady_clock::now()
        };
        return;
    } else if (event->mask & IN_MOVED_TO) {
        file_event.type = events::EventType::MOVED;

        std::lock_guard<std::mutex> lock(move_mutex);
        auto it = pending_moves.find(event->cookie);
        if (it != pending_moves.end()) {
            file_event.old_path = it->second.from_path;
            pending_moves.erase(it);
        } else {
            // file moved from outside observed dir tree to inside, treat it as create
            file_event.type = events::EventType::CREATED;
            if (!is_dir) {
                readFileContents(full_path, file_event.contents);
            }
            
            if (is_dir) {
                addWatch(full_path);
            }
        }
        
        if (is_dir) {
            addWatch(full_path);
        }
    } else {
        return;
    }

    std::lock_guard<std::mutex> lock(callback_mutex);
    if (callback) {
        callback(file_event);
    }
}

};