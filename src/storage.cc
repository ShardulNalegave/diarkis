
#include "diarkis/storage.h"
#include "spdlog/spdlog.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "fcntl.h"
#include "unistd.h"
#include "dirent.h"

namespace diarkis {

Storage::Storage(std::string base_path) : base_path_(std::move(base_path)) {
    if (!base_path_.empty() && base_path_.back() == '/') {
        base_path_.pop_back();
    }
}

int Storage::init() {
    struct stat st;
    if (::stat(base_path_.c_str(), &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            spdlog::error("Base path exists but is not a directory: {}", base_path_);
            return ENOTDIR;
        }
    } else {
        if (mkdir(base_path_.c_str(), 0755) != 0) {
            int err = errno;
            spdlog::error("Failed to create base directory {}: {}", base_path_, strerror(err));
            return err;
        }
    }

    spdlog::info("Storage initialized at base directory: {}", base_path_);
    return 0;
}

int Storage::create_file(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    
    if (errno == EEXIST) {
        return 0;
    }
    
    return errno;
}

int Storage::create_directory(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (mkdir(full_path.c_str(), 0755) == 0) {
        return 0;
    }
    
    if (errno == EEXIST) {
        return 0;
    }
    
    return errno;
}

size_t Storage::read_file(const std::string& path, uint8_t* buffer) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_RDONLY);
    if (fd < 0) {
        int err = errno;
        if (err == ENOENT) {
            spdlog::error("read_file: File not found (Path = {})", path);
            return 0;
        }
        spdlog::error("read_file: IO Error (Path = {})\n\t{}", path, strerror(errno));
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int err = errno;
        close(fd);
        spdlog::error("read_file: IO Error (Path = {})\n\t{}", path, strerror(errno));
        return 0;
    }

    ssize_t total_read = 0;
    while (total_read < st.st_size) {
        ssize_t n = read(fd, buffer + total_read, st.st_size - total_read);
        if (n < 0) {
            int err = errno;
            close(fd);
            spdlog::error("read_file: IO Error (Path = {})\n\t{}", path, strerror(errno));
            return 0;
        }
        if (n == 0) break;
        total_read += n;
    }

    close(fd);
    spdlog::debug("Read {} bytes from {}", total_read, path);
    return total_read;
}

int Storage::write_file(const std::string& path, uint8_t* buffer, size_t size) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return errno;
    
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t n = write(fd, buffer + total_written, size - total_written);
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

int Storage::append_file(const std::string& path, uint8_t* buffer, size_t size) {
    std::string full_path = get_full_path(path);
    
    int fd = open(full_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return errno;
    
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t n = write(fd, buffer + total_written, size - total_written);
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

int Storage::rename_file(const std::string& old_path, const std::string& new_path) {
    std::string full_old = get_full_path(old_path);
    std::string full_new = get_full_path(new_path);
    
    if (rename(full_old.c_str(), full_new.c_str()) == 0) {
        return 0;
    }
    
    return errno;
}

std::vector<std::string> Storage::list_directory(const std::string& path) {
    std::string full_path = get_full_path(path);
    std::vector<std::string> items;

    DIR* dir = opendir(full_path.c_str());
    if (!dir) {
        int err = errno;
        if (err == ENOENT) {
            spdlog::error("list_directory: Directory not found (Path = {})", path);
            return items;
        }
        spdlog::error("list_directory: IO Error (Path = {})", path);
        return items;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        items.push_back(name);
    }

    closedir(dir);
    return items;
}

int Storage::delete_file(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (unlink(full_path.c_str()) == 0) {
        return 0;
    }
    
    if (errno == ENOENT) {
        return 0;
    }
    
    return errno;
}

int Storage::delete_directory(const std::string& path) {
    std::string full_path = get_full_path(path);
    
    if (rmdir(full_path.c_str()) == 0) {
        return 0;
    }
    
    if (errno == ENOENT) {
        return 0;
    }
    
    return errno;
}

std::string Storage::get_full_path(const std::string& relative_path) const {
    std::string clean_path = relative_path;
    if (!clean_path.empty() && clean_path[0] == '/') {
        clean_path = clean_path.substr(1);
    }
    
    if (clean_path.empty()) {
        return base_path_;
    }
    
    return base_path_ + "/" + clean_path;
}

bool Storage::path_exists(const std::string& full_path) const {
    struct stat st;
    return ::stat(full_path.c_str(), &st) == 0;
}

bool Storage::is_directory(const std::string& full_path) const {
    struct stat st;
    if (::stat(full_path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

}
