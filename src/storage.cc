
#include "diarkis/storage.h"
#include "spdlog/spdlog.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace diarkis {

namespace {
    constexpr off_t MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB
    constexpr mode_t FILE_MODE = 0644;
    constexpr mode_t DIR_MODE = 0755;
    
    class FileDescriptor {
    public:
        explicit FileDescriptor(int fd) : fd_(fd) {}
        ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
        
        FileDescriptor(const FileDescriptor&) = delete;
        FileDescriptor& operator=(const FileDescriptor&) = delete;
        
        int get() const { return fd_; }
        bool valid() const { return fd_ >= 0; }
        int release() { int fd = fd_; fd_ = -1; return fd; }
        
    private:
        int fd_;
    };
    
    bool is_safe_path(const std::string& path) {
        if (!path.empty() && path[0] == '/') {
            return false;
        }
        
        std::vector<std::string> components;
        std::istringstream iss(path);
        std::string component;
        
        while (std::getline(iss, component, '/')) {
            if (component.empty() || component == ".") {
                continue;
            }
            
            if (component == "..") {
                return false;
            }
            
            components.push_back(component);
        }
        
        for (const auto& comp : components) {
            if (comp.find('\0') != std::string::npos) {
                return false;
            }
        }
        
        return true;
    }
    
    std::string normalize_path(const std::string& path) {
        std::string result;
        bool last_was_slash = false;
        
        for (char c : path) {
            if (c == '/') {
                if (!last_was_slash && !result.empty()) {
                    result += c;
                }
                last_was_slash = true;
            } else {
                result += c;
                last_was_slash = false;
            }
        }
        
        // Remove trailing slash
        if (!result.empty() && result.back() == '/') {
            result.pop_back();
        }
        
        return result;
    }
}

FileLocker::FileLocker() = default;
FileLocker::~FileLocker() = default;

void FileLocker::lock_read(const std::string& path) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (true) {
        auto it = locks_.find(path);
        
        if (it == locks_.end()) {
            // no lock exists, create a read lock
            locks_[path] = {1, false};
            return;
        }
        
        if (!it->second.write_locked) {
            // already read-locked, increment counter
            it->second.reader_count++;
            return;
        }
        
        // write lock exists, wait
        cv_.wait(lock);
    }
}

void FileLocker::unlock_read(const std::string& path) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = locks_.find(path);
    if (it == locks_.end()) {
        spdlog::warn("Attempted to unlock_read non-existent lock for: {}", path);
        return;
    }
    
    it->second.reader_count--;
    
    if (it->second.reader_count == 0) {
        locks_.erase(it);
    }
    
    cv_.notify_all();
}

void FileLocker::lock_write(const std::string& path) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (true) {
        auto it = locks_.find(path);
        
        if (it == locks_.end()) {
            // no lock exists, create a write lock
            locks_[path] = {0, true};
            return;
        }
        
        if (it->second.reader_count == 0 && !it->second.write_locked) {
            // no active locks, acquire write lock
            it->second.write_locked = true;
            return;
        }
        
        // lock exists, wait
        cv_.wait(lock);
    }
}

void FileLocker::unlock_write(const std::string& path) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto it = locks_.find(path);
    if (it == locks_.end()) {
        spdlog::warn("Attempted to unlock_write non-existent lock for: {}", path);
        return;
    }
    
    locks_.erase(it);
    cv_.notify_all();
}

ReadLock::ReadLock(FileLocker& locker, const std::string& path)
    : locker_(locker), path_(path) {
    locker_.lock_read(path_);
}

ReadLock::~ReadLock() {
    locker_.unlock_read(path_);
}

WriteLock::WriteLock(FileLocker& locker, const std::string& path)
    : locker_(locker), path_(path) {
    locker_.lock_write(path_);
}

WriteLock::~WriteLock() {
    locker_.unlock_write(path_);
}

Storage::Storage(std::string base_path) : base_path_(std::move(base_path)) {
    while (!base_path_.empty() && base_path_.back() == '/') {
        base_path_.pop_back();
    }
}

Result<void> Storage::init() {
    struct stat st;
    if (::stat(base_path_.c_str(), &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            spdlog::error("Base path exists but is not a directory: {}", base_path_);
            return Error(ErrorCode::NotDirectory, "Base path is not a directory");
        }
        spdlog::info("Storage initialized at existing directory: {}", base_path_);
        return Result<void>();
    }
    
    if (::mkdir(base_path_.c_str(), DIR_MODE) != 0) {
        int err = errno;
        spdlog::error("Failed to create base directory {}: {}", base_path_, std::strerror(err));
        return Error::from_errno(err);
    }
    
    spdlog::info("Storage initialized at new directory: {}", base_path_);
    return Result<void>();
}

std::string Storage::resolve_path(const std::string& relative_path) const {
    std::string clean = normalize_path(relative_path);
    while (!clean.empty() && clean[0] == '/') {
        clean = clean.substr(1);
    }
    
    if (clean.empty()) {
        return base_path_;
    }
    
    return base_path_ + "/" + clean;
}

Result<void> Storage::validate_path(const std::string& path) const {
    if (path.length() > 4096) {
        return Error(ErrorCode::InvalidPath, "Path too long");
    }
    
    if (!is_safe_path(path)) {
        spdlog::error("Path traversal attempt detected: {}", path);
        return Error(ErrorCode::InvalidPath, "Invalid path: contains path traversal");
    }
    
    return Result<void>();
}

Result<void> Storage::create_file(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    WriteLock file_lock(file_locker_, path);
    
    std::string full_path = resolve_path(path);
    FileDescriptor fd(::open(full_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, FILE_MODE));
    
    if (!fd.valid()) {
        int err = errno;
        if (err == EEXIST) {
            return Result<void>();
        }
        spdlog::error("Failed to create file {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    spdlog::debug("Created file: {}", path);
    return Result<void>();
}

Result<void> Storage::create_directory(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    std::string full_path = resolve_path(path);
    
    if (::mkdir(full_path.c_str(), DIR_MODE) == 0) {
        spdlog::debug("Created directory: {}", path);
        return Result<void>();
    }
    
    int err = errno;
    if (err == EEXIST) {
        return Result<void>();
    }
    
    spdlog::error("Failed to create directory {}: {}", path, std::strerror(err));
    return Error::from_errno(err);
}

Result<std::vector<uint8_t>> Storage::read_file(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation.error();
    }
    
    ReadLock file_lock(file_locker_, path);
    
    std::string full_path = resolve_path(path);
    FileDescriptor fd(::open(full_path.c_str(), O_RDONLY));
    
    if (!fd.valid()) {
        int err = errno;
        spdlog::error("Failed to open file {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    struct stat st;
    if (::fstat(fd.get(), &st) != 0) {
        int err = errno;
        spdlog::error("Failed to stat file {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    if (st.st_size > MAX_FILE_SIZE) {
        spdlog::error("File too large: {} ({} bytes)", path, st.st_size);
        return Error(ErrorCode::IoError, "File too large");
    }
    
    std::vector<uint8_t> buffer(st.st_size);
    size_t total_read = 0;
    
    while (total_read < static_cast<size_t>(st.st_size)) {
        ssize_t n = ::read(fd.get(), buffer.data() + total_read, st.st_size - total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            spdlog::error("Failed to read file {}: {}", path, std::strerror(err));
            return Error::from_errno(err);
        }
        if (n == 0) break;
        total_read += n;
    }
    
    buffer.resize(total_read);
    spdlog::debug("Read {} bytes from {}", total_read, path);
    return buffer;
}

Result<void> Storage::write_file(const std::string& path, const uint8_t* buffer, size_t size) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    WriteLock file_lock(file_locker_, path);
    
    std::string full_path = resolve_path(path);
    FileDescriptor fd(::open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE));
    
    if (!fd.valid()) {
        int err = errno;
        spdlog::error("Failed to open file for writing {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t n = ::write(fd.get(), buffer + total_written, size - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            spdlog::error("Failed to write to file {}: {}", path, std::strerror(err));
            return Error::from_errno(err);
        }
        total_written += n;
    }
    
    if (::fsync(fd.get()) != 0) {
        int err = errno;
        spdlog::error("Failed to sync file {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    spdlog::debug("Wrote {} bytes to {}", size, path);
    return Result<void>();
}

Result<void> Storage::append_file(const std::string& path, const uint8_t* buffer, size_t size) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    WriteLock file_lock(file_locker_, path);
    
    std::string full_path = resolve_path(path);
    FileDescriptor fd(::open(full_path.c_str(), O_WRONLY | O_APPEND | O_CREAT, FILE_MODE));
    
    if (!fd.valid()) {
        int err = errno;
        spdlog::error("Failed to open file for appending {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t n = ::write(fd.get(), buffer + total_written, size - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            spdlog::error("Failed to append to file {}: {}", path, std::strerror(err));
            return Error::from_errno(err);
        }
        total_written += n;
    }
    
    if (::fsync(fd.get()) != 0) {
        int err = errno;
        spdlog::error("Failed to sync file {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    spdlog::debug("Appended {} bytes to {}", size, path);
    return Result<void>();
}

Result<void> Storage::rename(const std::string& old_path, const std::string& new_path) {
    auto validation = validate_path(old_path);
    if (!validation.ok()) {
        return validation;
    }
    
    validation = validate_path(new_path);
    if (!validation.ok()) {
        return validation;
    }
    
    WriteLock old_lock(file_locker_, old_path);
    WriteLock new_lock(file_locker_, new_path);
    
    std::string full_old = resolve_path(old_path);
    std::string full_new = resolve_path(new_path);
    
    if (::rename(full_old.c_str(), full_new.c_str()) == 0) {
        spdlog::debug("Renamed {} to {}", old_path, new_path);
        return Result<void>();
    }
    
    int err = errno;
    spdlog::error("Failed to rename {} to {}: {}", old_path, new_path, std::strerror(err));
    return Error::from_errno(err);
}

Result<void> Storage::delete_file(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    WriteLock file_lock(file_locker_, path);
    
    std::string full_path = resolve_path(path);
    
    if (::unlink(full_path.c_str()) == 0) {
        spdlog::debug("Deleted file: {}", path);
        return Result<void>();
    }
    
    int err = errno;
    if (err == ENOENT) {
        return Result<void>();
    }
    
    spdlog::error("Failed to delete file {}: {}", path, std::strerror(err));
    return Error::from_errno(err);
}

Result<void> Storage::delete_directory(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation;
    }
    
    std::string full_path = resolve_path(path);
    
    if (::rmdir(full_path.c_str()) == 0) {
        spdlog::debug("Deleted directory: {}", path);
        return Result<void>();
    }
    
    int err = errno;
    if (err == ENOENT) {
        return Result<void>();
    }
    
    spdlog::error("Failed to delete directory {}: {}", path, std::strerror(err));
    return Error::from_errno(err);
}

Result<std::vector<FileInfo>> Storage::list_directory(const std::string& path) {
    auto validation = validate_path(path);
    if (!validation.ok()) {
        return validation.error();
    }
    
    std::string full_path = resolve_path(path);
    std::vector<FileInfo> items;
    
    DIR* dir = ::opendir(full_path.c_str());
    if (!dir) {
        int err = errno;
        spdlog::error("Failed to open directory {}: {}", path, std::strerror(err));
        return Error::from_errno(err);
    }
    
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        FileInfo info;
        info.name = name;
        
        std::string entry_path = full_path + "/" + name;
        struct stat st;
        if (::stat(entry_path.c_str(), &st) == 0) {
            info.is_directory = S_ISDIR(st.st_mode);
            info.size = st.st_size;
        } else {
            info.is_directory = false;
            info.size = 0;
        }
        
        items.push_back(std::move(info));
    }
    
    ::closedir(dir);
    spdlog::debug("Listed {} items in {}", items.size(), path);
    return items;
}

}
