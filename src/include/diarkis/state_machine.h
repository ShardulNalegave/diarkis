
#ifndef DIARKIS_STATE_MACHINE_H
#define DIARKIS_STATE_MACHINE_H

#include <atomic>
#include <memory>
#include <string>
#include "braft/raft.h"
#include "braft/storage.h"
#include "braft/util.h"
#include "brpc/server.h"
#include "diarkis/storage.h"
#include "diarkis/commands.h"
#include "diarkis/raft_closure.h"

namespace diarkis {

class StateMachine : public braft::StateMachine {
public:
    struct Options {
        std::string base_path;
        std::string raft_path;
        std::string group_id;
        braft::PeerId peer_id;
        std::string initial_conf;
        int election_timeout_ms = 5000;
        int snapshot_interval_s = 3600;
        
        // Validation
        bool validate() const;
    };

    explicit StateMachine(const Options& opts);
    ~StateMachine() override;
    
    // Disable copy, enable move
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    Result<void> init();
    void shutdown();

    bool is_leader() const;
    braft::PeerId leader_id() const;

    // Command application
    commands::Response apply_write_command(const commands::Command& cmd);
    commands::Response apply_read_command(const commands::Command& cmd);

    // bRaft StateMachine interface
    void on_apply(braft::Iterator& iter) override;
    void on_shutdown() override;
    void on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) override;
    int on_snapshot_load(braft::SnapshotReader* reader) override;
    void on_leader_start(int64_t term) override;
    void on_leader_stop(const butil::Status& status) override;
    void on_error(const braft::Error& e) override;
    void on_configuration_committed(const braft::Configuration& conf) override;
    void on_start_following(const braft::LeaderChangeContext& ctx) override;
    void on_stop_following(const braft::LeaderChangeContext& ctx) override;

private:
    Result<void> init_storage();
    Result<void> init_raft_directories();
    Result<void> init_raft_node();
    Result<void> init_brpc_server();
    
    void apply_command(const commands::Command& cmd, RaftClosure* done);
    commands::Response handle_read_file(const commands::Command& cmd);
    commands::Response handle_list_directory(const commands::Command& cmd);
    
    Options options_;
    std::unique_ptr<Storage> storage_;
    std::unique_ptr<braft::Node> raft_node_;
    std::unique_ptr<brpc::Server> brpc_server_;
    std::atomic<bool> is_leader_;
};

}

#endif
