
#ifndef DIARKIS_LOCAL_STORAGE_H
#define DIARKIS_LOCAL_STORAGE_H

#include "diarkis/fs_operations.h"
#include "diarkis/fs_client.h"
#include <string>
#include <vector>

namespace diarkis {

/**
 * Local storage engine that executes filesystem operations
 * 
 * All operations are performed within a base directory.
 * Paths are relative to this base directory for safety.
 * 
 * Operations are designed to be idempotent where possible.
 */
class LocalStorageEngine {
public:
    explicit LocalStorageEngine(std::string base_path);

    /**
     * Initialize the storage directory
     */
    int initialize();

    /**
     * Apply a filesystem operation (called after Raft consensus)
     */
    int apply_operation(const FSOperation& op);

    /**
     * Read file contents
     */
    Result<std::vector<uint8_t>> read_file(const std::string& path);

    /**
     * List directory contents
     */
    Result<std::vector<std::string>> list_directory(const std::string& path);

    /**
     * Get file/directory metadata
     */
    Result<FileInfo> stat(const std::string& path);

    /**
     * Check if path exists
     */
    Result<bool> exists(const std::string& path);

    const std::string& get_base_path() const { return base_path_; }

private:
    std::string base_path_;

    int do_create_file(const std::string& path);
    int do_write_file(const std::string& path, const std::vector<uint8_t>& data);
    int do_append_file(const std::string& path, const std::vector<uint8_t>& data);
    int do_delete_file(const std::string& path);
    int do_create_directory(const std::string& path);
    int do_delete_directory(const std::string& path);
    int do_rename(const std::string& old_path, const std::string& new_path);

    std::string get_full_path(const std::string& relative_path) const;
    bool path_exists(const std::string& full_path) const;
    bool is_directory(const std::string& full_path) const;
};

} // namespace diarkis

#endif /* DIARKIS_LOCAL_STORAGE_H */
