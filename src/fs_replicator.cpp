
#include "diarkis/fs_replicator.h"

#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace fs {

Replicator::Replicator(const std::string& root_dir_, fs::Watcher* watcher_)
    : root_dir(root_dir_), watcher(watcher_) {
    if (!root_dir.empty() && root_dir.back() == '/') {
        root_dir.pop_back();
    }
}

void Replicator::ensureParentDirectory(const std::string& path) {
    size_t last_slash = path.find_last_of('/');
    if (last_slash == std::string::npos) {
        return;
    }
    
    std::string parent = path.substr(0, last_slash);
    
    struct stat st;
    if (stat(parent.c_str(), &st) == 0) {
        return;
    }
    
    ensureParentDirectory(parent);
    
    if (watcher) {
        watcher->ignoreNextEvent(parent);
    }
    
    if (mkdir(parent.c_str(), 0755) != 0) {
        spdlog::error("Failed to create parent directory: {}\n\t{}", parent, strerror(errno));
    }
}

bool Replicator::createFile(const std::string& path, const std::string& contents) {
    ensureParentDirectory(path);
    
    if (watcher) {
        watcher->ignoreNextEvent(path);
    }
    
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        spdlog::error("Failed to create file: {}", path);
        return false;
    }
    
    if (!contents.empty()) {
        file.write(contents.c_str(), contents.size());
        if (!file) {
            spdlog::error("Failed to write file contents: {}", path);
            return false;
        }
    }
    
    file.close();
    spdlog::info("Created file: {} ({} bytes)", path, contents.size());
    return true;
}

bool Replicator::createDirectory(const std::string& path) {
    ensureParentDirectory(path);
    
    if (watcher) {
        watcher->ignoreNextEvent(path);
    }
    
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno == EEXIST) {
            spdlog::debug("Directory already exists: {}", path);
            return true;
        }
        spdlog::error("Failed to create directory: {}\n\t{}", path, strerror(errno));
        return false;
    }
    
    spdlog::info("Created directory: {}", path);
    return true;
}

bool Replicator::deleteFile(const std::string& path) {
    if (watcher) {
        watcher->ignoreNextEvent(path);
    }
    
    if (unlink(path.c_str()) != 0) {
        if (errno == ENOENT) {
            spdlog::debug("File already deleted: {}", path);
            return true;
        }
        spdlog::error("Failed to delete file: {}\n\t{}", path, strerror(errno));
        return false;
    }
    
    spdlog::info("Deleted file: {}", path);
    return true;
}

bool Replicator::deleteDirectory(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            std::string full_path = path + "/" + entry->d_name;
            
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    deleteDirectory(full_path);
                } else {
                    deleteFile(full_path);
                }
            }
        }
        closedir(dir);
    }
    
    if (watcher) {
        watcher->ignoreNextEvent(path);
    }
    
    if (rmdir(path.c_str()) != 0) {
        if (errno == ENOENT) {
            spdlog::debug("Directory already deleted: {}", path);
            return true;
        }
        spdlog::error("Failed to delete directory: {}\n\t{}", path, strerror(errno));
        return false;
    }
    
    spdlog::info("Deleted directory: {}", path);
    return true;
}

bool Replicator::modifyFile(const std::string& path, const std::string& contents) {
    if (watcher) {
        watcher->ignoreNextEvent(path);
    }
    
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        spdlog::error("Failed to open file for modification: {}", path);
        return false;
    }
    
    if (!contents.empty()) {
        file.write(contents.c_str(), contents.size());
        if (!file) {
            spdlog::error("Failed to write modified contents: {}", path);
            return false;
        }
    }
    
    file.close();
    spdlog::info("Modified file: {} ({} bytes)", path, contents.size());
    return true;
}

bool Replicator::moveFile(const std::string& old_path, const std::string& new_path) {
    ensureParentDirectory(new_path);
    
    if (watcher) {
        watcher->ignoreNextEvent(old_path);
        watcher->ignoreNextEvent(new_path);
    }
    
    if (rename(old_path.c_str(), new_path.c_str()) != 0) {
        spdlog::error("Failed to move file: {} -> {}\n\t{}", old_path, new_path, strerror(errno));
        return false;
    }
    
    spdlog::info("Moved file: {} -> {}", old_path, new_path);
    return true;
}

bool Replicator::moveDirectory(const std::string& old_path, const std::string& new_path) {
    ensureParentDirectory(new_path);
    
    if (watcher) {
        watcher->ignoreNextEvent(old_path);
        watcher->ignoreNextEvent(new_path);
    }
    
    if (rename(old_path.c_str(), new_path.c_str()) != 0) {
        spdlog::error("Failed to move directory: {} -> {}\n\t{}", old_path, new_path, strerror(errno));
        return false;
    }
    
    spdlog::info("Moved directory: {} -> {}", old_path, new_path);
    return true;
}

bool Replicator::applyEvent(const events::Event& event) {
    std::string full_path = root_dir + "/" + event.relative_path;
    
    switch (event.type) {
        case events::EventType::CREATED:
            if (event.is_dir) {
                return createDirectory(full_path);
            } else {
                return createFile(full_path, event.contents);
            }
            
        case events::EventType::MODIFIED:
            if (!event.is_dir) {
                return modifyFile(full_path, event.contents);
            }
            return true;
            
        case events::EventType::DELETED:
            if (event.is_dir) {
                return deleteDirectory(full_path);
            } else {
                return deleteFile(full_path);
            }
            
        case events::EventType::MOVED:
            if (!event.old_path.empty()) {
                std::string old_full_path = event.old_path;
                if (event.is_dir) {
                    return moveDirectory(old_full_path, full_path);
                } else {
                    return moveFile(old_full_path, full_path);
                }
            }
            spdlog::warn("MOVED event without old_path, treating as CREATE");
            if (event.is_dir) {
                return createDirectory(full_path);
            } else {
                return createFile(full_path, event.contents);
            }
            
        case events::EventType::INVALID:
            spdlog::error("Cannot apply INVALID event");
            return false;
            
        default:
            spdlog::error("Unknown event type: {}", static_cast<int>(event.type));
            return false;
    }
}

};
