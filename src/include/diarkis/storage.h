
#ifndef DIARKIS_STORAGE_H
#define DIARKIS_STORAGE_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include "diarkis/result.h"

namespace diarkis {

struct FileInfo {
    std::string name;
    bool is_directory;
    size_t size;
};

class FileLocker {
public:
    FileLocker();
    ~FileLocker();
    
    FileLocker(const FileLocker&) = delete;
    FileLocker& operator=(const FileLocker&) = delete;
    
    void lock_read(const std::string& path);
    void unlock_read(const std::string& path);
    
    void lock_write(const std::string& path);
    void unlock_write(const std::string& path);

private:
    struct LockState {
        int reader_count = 0;
        bool write_locked = false;
    };
    
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, LockState> locks_;
};

class ReadLock {
public:
    ReadLock(FileLocker& locker, const std::string& path);
    ~ReadLock();
    
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;

private:
    FileLocker& locker_;
    std::string path_;
};

class WriteLock {
public:
    WriteLock(FileLocker& locker, const std::string& path);
    ~WriteLock();
    
    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;

private:
    FileLocker& locker_;
    std::string path_;
};

class Storage {
public:
    explicit Storage(std::string base_path);
    ~Storage() = default;
    
    // disable copy, enable move
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = default;
    Storage& operator=(Storage&&) = default;
    
    Result<void> init();
    
    Result<void> create_file(const std::string& path);
    Result<void> create_directory(const std::string& path);
    
    Result<std::vector<uint8_t>> read_file(const std::string& path);
    Result<void> write_file(const std::string& path, const uint8_t* buffer, size_t size);
    Result<void> append_file(const std::string& path, const uint8_t* buffer, size_t size);
    
    Result<void> rename(const std::string& old_path, const std::string& new_path);
    Result<void> delete_file(const std::string& path);
    Result<void> delete_directory(const std::string& path);
    
    Result<std::vector<FileInfo>> list_directory(const std::string& path);
    
    const std::string& base_path() const { return base_path_; }

private:
    std::string resolve_path(const std::string& relative_path) const;
    Result<void> validate_path(const std::string& path) const;
    
    std::string base_path_;
    FileLocker file_locker_;
};

}

#endif
