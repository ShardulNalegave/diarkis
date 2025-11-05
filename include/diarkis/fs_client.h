
#ifndef DIARKIS_FS_CLIENT_H
#define DIARKIS_FS_CLIENT_H

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace diarkis {

class RaftFilesystemService;

/**
 * Status codes for filesystem operations
 */
enum class FSStatus {
    OK = 0,
    NOT_FOUND,           // File/directory doesn't exist
    ALREADY_EXISTS,      // File/directory already exists
    NOT_LEADER,          // This node is not the leader (for write ops)
    NO_LEADER,           // No leader elected yet
    IO_ERROR,            // Underlying I/O error
    INVALID_PATH,        // Invalid path format
    NOT_DIRECTORY,       // Path is not a directory
    DIRECTORY_NOT_EMPTY, // Cannot delete non-empty directory
    RAFT_ERROR           // Raft consensus error
};

template<typename T>
struct Result {
    FSStatus status;
    T value;
    std::string error_message;

    bool ok() const { return status == FSStatus::OK; }
    
    static Result<T> Ok(T val) {
        return Result<T>{FSStatus::OK, std::move(val), ""};
    }
    
    static Result<T> Error(FSStatus s, const std::string& msg = "") {
        return Result<T>{s, T{}, msg};
    }
};

// specialization for void operations
template<>
struct Result<void> {
    FSStatus status;
    std::string error_message;

    bool ok() const { return status == FSStatus::OK; }
    
    static Result<void> Ok() {
        return Result<void>{FSStatus::OK, ""};
    }
    
    static Result<void> Error(FSStatus s, const std::string& msg = "") {
        return Result<void>{s, msg};
    }
};

struct FileInfo {
    std::string name;
    size_t size;
    bool is_directory;
    uint64_t last_modified;  // Unix timestamp
};

/**
 * Client library for interacting with the replicated filesystem
 * 
 * Write operations (create, write, delete, etc.) must go through the leader
 * and are replicated via Raft consensus.
 * 
 * Read operations can be performed on any node, but followers check their
 * commit index to ensure they're not too far behind before serving reads.
 */
class Client {
public:
    struct Config {
        std::string data_path;
        std::string raft_path;
        std::string group_id;
        std::string peer_id;           // ip:port:index format
        std::string initial_conf;      // Comma-separated peer list
        int election_timeout_ms = 5000;
        int snapshot_interval = 3600;
    };

    explicit Client(const Config& config);
    ~Client();

    /**
     * Initialize the filesystem client
     * This starts the Raft node and waits for it to be ready
     */
    Result<void> init();
    
    /**
     * Shutdown the filesystem client
     */
    void shutdown();
    
    /**
     * Create a new file
     * Returns OK if file is created or already exists
     */
    Result<void> create_file(const std::string& path);
    
    /**
     * Write data to a file (overwrites existing content)
     * Creates the file if it doesn't exist
     */
    Result<void> write_file(const std::string& path, const std::vector<uint8_t>& data);
    Result<void> write_file(const std::string& path, const std::string& data);
    
    /**
     * Append data to a file
     * Creates the file if it doesn't exist
     */
    Result<void> append_file(const std::string& path, const std::vector<uint8_t>& data);
    Result<void> append_file(const std::string& path, const std::string& data);
    
    /**
     * Delete a file
     * Returns OK if file is deleted or doesn't exist
     */
    Result<void> delete_file(const std::string& path);
    
    /**
     * Create a directory
     * Returns OK if directory is created or already exists
     */
    Result<void> create_directory(const std::string& path);
    
    /**
     * Delete a directory (must be empty)
     */
    Result<void> delete_directory(const std::string& path);
    
    /**
     * Rename/move a file or directory
     */
    Result<void> rename(const std::string& old_path, const std::string& new_path);
    
    /**
     * Read entire file contents
     */
    Result<std::vector<uint8_t>> read_file(const std::string& path);
    Result<std::string> read_file_string(const std::string& path);
    
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
    
    /**
     * Check if this node is currently the leader
     */
    bool is_leader() const;
    
    /**
     * Get the current leader's peer ID
     * Returns empty string if no leader is elected
     */
    std::string get_leader() const;

private:
    Config config_;
    std::unique_ptr<RaftFilesystemService> service_;
};

} // namespace diarkis

#endif /* DIARKIS_FS_CLIENT_H */
