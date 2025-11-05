
#include "diarkis/local_storage.h"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <ctime>

namespace diarkis {

LocalStorageEngine::LocalStorageEngine(std::string base_path)
    : base_path_(std::move(base_path)) {
    if (!base_path_.empty() && base_path_.back() == '/') {
        base_path_.pop_back();
    }
}

int LocalStorageEngine::initialize() {
    struct stat st;
    if (::stat(base_path_.c_str(), &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            spdlog::error("Base path exists but is not a directory: {}", base_path_);
            return ENOTDIR;
        }
        spdlog::info("Storage initialized at: {}", base_path_);
        return 0;
    }

    if (mkdir(base_path_.c_str(), 0755) != 0) {
        int err = errno;
        spdlog::error("Failed to create base directory {}: {}", base_path_, strerror(err));
        return err;
    }

    spdlog::info("Storage initialized at new directory: {}", base_path_);
    return 0;
}

int LocalStorageEngine::apply_operation(const FSOperation& op) {
    spdlog::debug("Applying: {}", op.to_string());

    int result = 0;
    switch (op.type) {
        case FSOperationType::CREATE_FILE:
            result = do_create_file(op.path);
            break;
        case FSOperationType::WRITE_FILE:
            result = do_write_file(op.path, op.data);
            break;
        case FSOperationType::APPEND_FILE:
            result = do_append_file(op.path, op.data);
            break;
        case FSOperationType::DELETE_FILE:
            result = do_delete_file(op.path);
            break;
        case FSOperationType::CREATE_DIR:
            result = do_create_directory(op.path);
            break;
        case FSOperationType::DELETE_DIR:
            result = do_delete_directory(op.path);
            break;
        case FSOperationType::RENAME:
            if (op.data.empty()) {
                result = EINVAL;
            } else {
                std::string new_path(op.data.begin(), op.data.end());
                result = do_rename(op.path, new_path);
            }
            break;
        default:
            result = EINVAL;
    }

    if (result != 0) {
        spdlog::error("Operation failed: {}, error: {}", op.to_string(), strerror(result));
    }
    return result;
}

Result<std::vector<uint8_t>> LocalStorageEngine::read_file(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_RDONLY);
    if (fd < 0) {
        int err = errno;
        if (err == ENOENT) {
            return Result<std::vector<uint8_t>>::Error(FSStatus::NOT_FOUND, 
                "File not found: " + path);
        }
        return Result<std::vector<uint8_t>>::Error(FSStatus::IO_ERROR, strerror(err));
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int err = errno;
        close(fd);
        return Result<std::vector<uint8_t>>::Error(FSStatus::IO_ERROR, strerror(err));
    }

    std::vector<uint8_t> buffer(st.st_size);
    ssize_t total_read = 0;
    while (total_read < st.st_size) {
        ssize_t n = read(fd, buffer.data() + total_read, st.st_size - total_read);
        if (n < 0) {
            int err = errno;
            close(fd);
            return Result<std::vector<uint8_t>>::Error(FSStatus::IO_ERROR, strerror(err));
        }
        if (n == 0) break;
        total_read += n;
    }

    close(fd);
    spdlog::debug("Read {} bytes from {}", total_read, path);
    return Result<std::vector<uint8_t>>::Ok(std::move(buffer));
}

Result<std::vector<std::string>> LocalStorageEngine::list_directory(const std::string& path) {
    std::string full_path = get_full_path(path);

    DIR* dir = opendir(full_path.c_str());
    if (!dir) {
        int err = errno;
        if (err == ENOENT) {
            return Result<std::vector<std::string>>::Error(FSStatus::NOT_FOUND, 
                "Directory not found: " + path);
        }
        return Result<std::vector<std::string>>::Error(FSStatus::IO_ERROR, strerror(err));
    }

    std::vector<std::string> entries;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        entries.push_back(name);
    }

    closedir(dir);
    return Result<std::vector<std::string>>::Ok(std::move(entries));
}

Result<FileInfo> LocalStorageEngine::stat(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    struct stat st;
    if (::stat(full_path.c_str(), &st) != 0) {
        int err = errno;
        if (err == ENOENT) {
            return Result<FileInfo>::Error(FSStatus::NOT_FOUND, "Path not found: " + path);
        }
        return Result<FileInfo>::Error(FSStatus::IO_ERROR, strerror(err));
    }

    FileInfo info;
    info.name = path.substr(path.find_last_of('/') + 1);
    info.size = st.st_size;
    info.is_directory = S_ISDIR(st.st_mode);
    info.last_modified = st.st_mtime;

    return Result<FileInfo>::Ok(std::move(info));
}

Result<bool> LocalStorageEngine::exists(const std::string& path) {
    std::string full_path = get_full_path(path);
    return Result<bool>::Ok(path_exists(full_path));
}

int LocalStorageEngine::do_create_file(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    
    if (errno == EEXIST) {
        return 0;  // Idempotent
    }
    
    return errno;
}

int LocalStorageEngine::do_write_file(const std::string& path, 
                                      const std::vector<uint8_t>& data) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return errno;
    
    size_t total_written = 0;
    while (total_written < data.size()) {
        ssize_t n = write(fd, data.data() + total_written, data.size() - total_written);
        if (n < 0) {
            int err = errno;
            close(fd);
            return err;
        }
        total_written += n;
    }
    
    if (fsync(fd) != 0) {
        int err = errno;
        close(fd);
        return err;
    }
    
    close(fd);
    return 0;
}

int LocalStorageEngine::do_append_file(const std::string& path, 
                                       const std::vector<uint8_t>& data) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return errno;
    
    size_t total_written = 0;
    while (total_written < data.size()) {
        ssize_t n = write(fd, data.data() + total_written, data.size() - total_written);
        if (n < 0) {
            int err = errno;
            close(fd);
            return err;
        }
        total_written += n;
    }
    
    if (fsync(fd) != 0) {
        int err = errno;
        close(fd);
        return err;
    }
    
    close(fd);
    return 0;
}

int LocalStorageEngine::do_delete_file(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (unlink(full_path.c_str()) == 0) {
        return 0;
    }
    
    if (errno == ENOENT) {
        return 0;  // Idempotent
    }
    
    return errno;
}

int LocalStorageEngine::do_create_directory(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (mkdir(full_path.c_str(), 0755) == 0) {
        return 0;
    }
    
    if (errno == EEXIST) {
        return 0;  // Idempotent
    }
    
    return errno;
}

int LocalStorageEngine::do_delete_directory(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (rmdir(full_path.c_str()) == 0) {
        return 0;
    }
    
    if (errno == ENOENT) {
        return 0;  // Idempotent
    }
    
    return errno;
}

int LocalStorageEngine::do_rename(const std::string& old_path, 
                                  const std::string& new_path) {
    std::string full_old = get_full_path(old_path);
    std::string full_new = get_full_path(new_path);
    
    if (rename(full_old.c_str(), full_new.c_str()) == 0) {
        return 0;
    }
    
    return errno;
}

std::string LocalStorageEngine::get_full_path(const std::string& relative_path) const {
    std::string clean_path = relative_path;
    if (!clean_path.empty() && clean_path[0] == '/') {
        clean_path = clean_path.substr(1);
    }
    
    if (clean_path.empty()) {
        return base_path_;
    }
    
    return base_path_ + "/" + clean_path;
}

bool LocalStorageEngine::path_exists(const std::string& full_path) const {
    struct stat st;
    return ::stat(full_path.c_str(), &st) == 0;
}

bool LocalStorageEngine::is_directory(const std::string& full_path) const {
    struct stat st;
    if (::stat(full_path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

} // namespace diarkis
