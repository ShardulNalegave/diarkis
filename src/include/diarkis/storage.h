
#ifndef DIARKIS_STORAGE_H
#define DIARKIS_STORAGE_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>
#include "diarkis/result.h"

namespace diarkis {

struct FileInfo {
    std::string name;
    bool is_directory;
    size_t size;
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
    Result<void> ensure_parent_directory(const std::string& path) const;
    
    std::string base_path_;
    mutable std::mutex io_mutex_;
};

}

#endif
