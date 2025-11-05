
#ifndef DIARKIS_FS_CLIENT_H
#define DIARKIS_FS_CLIENT_H

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

namespace diarkis {

class RaftFilesystemService;

/**
 * Client library for interacting with the replicated filesystem
 * 
 * This provides a simple API that applications can use.
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

    int init();
    void shutdown();

    // Write operations (require consensus)
    int create_file(const std::string& path);
    int write_file(const std::string& path, const std::vector<uint8_t>& data);
    int write_file(const std::string& path, const std::string& data);
    int append_file(const std::string& path, const std::vector<uint8_t>& data);
    int append_file(const std::string& path, const std::string& data);
    int delete_file(const std::string& path);
    int create_directory(const std::string& path);
    int delete_directory(const std::string& path);
    int rename(const std::string& old_path, const std::string& new_path);

    // Read operations (local)
    ssize_t read_file(const std::string& path, std::vector<uint8_t>& buffer);
    ssize_t read_file(const std::string& path, std::string& content);
    std::vector<std::string> list_directory(const std::string& path);

    // Status operations
    bool is_leader() const;
    std::string get_leader() const;

private:
    Config config_;
    std::unique_ptr<RaftFilesystemService> service_;
};

} // namespace diarkis

#endif /* DIARKIS_FS_CLIENT_H */
