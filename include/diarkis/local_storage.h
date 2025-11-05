
#ifndef DIARKIS_LOCAL_STORAGE_H
#define DIARKIS_LOCAL_STORAGE_H

#include "diarkis/fs_operations.h"
#include <string>
#include <vector>

namespace diarkis {

/**
 * Local storage engine that executes filesystem operations
 * 
 * All operations are performed within a base directory.
 * Paths are relative to this base directory for safety.
 * 
 * Operations are designed to be idempotent where possible
 * (e.g., creating an existing file returns success).
 */
class LocalStorageEngine {
public:
    /**
     * Initialize storage engine
     * @param base_path Base directory for all filesystem operations
     */
    explicit LocalStorageEngine(std::string base_path);

    /**
     * Initialize the storage directory
     * Creates base directory if it doesn't exist
     * @return 0 on success, errno on failure
     */
    int initialize();

    /**
     * Apply a filesystem operation
     * This is called by the Raft state machine after consensus
     * 
     * @param op Operation to apply
     * @return 0 on success, errno on failure
     */
    int apply_operation(const FSOperation& op);

    /**
     * Read file contents (local operation, no consensus needed)
     * @param path Relative path to file
     * @param buffer Output buffer
     * @return Number of bytes read on success, negative errno on failure
     */
    ssize_t read_file(const std::string& path, std::vector<uint8_t>& buffer);

    /**
     * List directory contents (local operation, no consensus needed)
     * @param path Relative path to directory
     * @return Vector of filenames, or empty vector on error
     */
    std::vector<std::string> list_directory(const std::string& path);

    const std::string& get_base_path() const { return base_path_; }

private:
    std::string base_path_;

    int do_create_file(const std::string& path);
    int do_write_file(const std::string& path, const std::vector<uint8_t>& data);
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
