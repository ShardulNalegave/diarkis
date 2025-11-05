
#ifndef DIARKIS_RAFT_FS_SERVICE_H
#define DIARKIS_RAFT_FS_SERVICE_H

#include "diarkis/fs_operations.h"
#include "diarkis/local_storage.h"
#include <braft/raft.h>
#include <braft/storage.h>
#include <braft/util.h>
#include <braft/protobuf_file.h>
#include <brpc/server.h>
#include <bthread/bthread.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace diarkis {

/**
 * Raft-based replicated filesystem service
 * 
 * This is the core state machine that integrates with braft.
 * All write operations go through Raft consensus before being applied.
 * Read operations are served locally without consensus.
 */
class RaftFilesystemService : public braft::StateMachine {
public:
    struct Options {
        std::string data_path;          // Base path for filesystem data
        std::string raft_path;          // Path for Raft metadata/logs
        std::string group_id;           // Raft group identifier
        braft::PeerId peer_id;          // This node's peer ID (ip:port:index)
        std::string initial_conf;       // Initial cluster configuration
        int election_timeout_ms = 5000;
        int snapshot_interval = 3600;   // Snapshot every N seconds
    };

    explicit RaftFilesystemService(const Options& options);
    ~RaftFilesystemService();

    int start();
    void shutdown();

    bool is_leader() const;
    braft::PeerId get_leader() const;

    // Write operations (require consensus)
    int create_file(const std::string& path);
    int write_file(const std::string& path, const std::vector<uint8_t>& data);
    int append_file(const std::string& path, const std::vector<uint8_t>& data);
    int delete_file(const std::string& path);
    int create_directory(const std::string& path);
    int delete_directory(const std::string& path);
    int rename(const std::string& old_path, const std::string& new_path);

    // Read operations (local, no consensus)
    ssize_t read_file(const std::string& path, std::vector<uint8_t>& buffer);
    std::vector<std::string> list_directory(const std::string& path);

    // braft::StateMachine interface implementation
    void on_apply(braft::Iterator& iter) override;
    void on_shutdown() override;
    void on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) override;
    int on_snapshot_load(braft::SnapshotReader* reader) override;
    void on_leader_start(int64_t term) override;
    void on_leader_stop(const butil::Status& status) override;
    void on_error(const ::braft::Error& e) override;
    void on_configuration_committed(const ::braft::Configuration& conf) override;
    void on_start_following(const ::braft::LeaderChangeContext& ctx) override;
    void on_stop_following(const ::braft::LeaderChangeContext& ctx) override;

private:
    Options options_;
    std::unique_ptr<LocalStorageEngine> storage_;
    std::unique_ptr<braft::Node> node_;
    std::unique_ptr<brpc::Server> server_;
    std::atomic<bool> is_leader_;
    std::atomic<int64_t> leader_term_;
    bthread_mutex_t mutex_;

    // Submit an operation through Raft for consensus
    int submit_operation(const FSOperation& op);

    // Helper to redirect to leader if not leader
    int redirect_error() const;
};

/**
 * Closure for async Raft operations
 */
class FSOperationClosure : public braft::Closure {
public:
    FSOperationClosure() : result(0), done(false) {}
    
    ~FSOperationClosure() override = default;

    void Run() override {
        std::unique_lock<std::mutex> lock(mutex);
        done = true;
        cond.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        while (!done) {
            cond.wait(lock);
        }
    }

    std::mutex mutex;
    std::condition_variable cond;
    bool done;
    int result;
    butil::Status status_code;
};

} // namespace diarkis

#endif /* DIARKIS_RAFT_FS_SERVICE_H */
