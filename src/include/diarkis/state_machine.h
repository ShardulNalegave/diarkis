
#ifndef DIARKIS_STATE_MACHINE_H
#define DIARKIS_STATE_MACHINE_H

#include <atomic>
#include <condition_variable>
#include "braft/raft.h"
#include "braft/storage.h"
#include "braft/util.h"
#include "brpc/server.h"
#include "diarkis/storage.h"

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
        int snapshot_interval = 3600;
    };

    explicit StateMachine(const Options& opts);
    ~StateMachine();

    int init();
    void shutdown();

    bool is_leader() const;
    braft::PeerId get_leader() const;

    void on_apply(braft::Iterator& iter) override;
    void on_shutdown() override;
    void on_leader_start(int64_t term) override;
    void on_leader_stop(const butil::Status& status) override;
    void on_error(const ::braft::Error& e) override;
    void on_configuration_committed(const ::braft::Configuration& conf) override;
    void on_start_following(const ::braft::LeaderChangeContext& ctx) override;
    void on_stop_following(const ::braft::LeaderChangeContext& ctx) override;

private:
    Options options_;
    std::unique_ptr<braft::Node> node_;
    std::unique_ptr<brpc::Server> server_;
    std::unique_ptr<Storage> storage_;

    std::atomic<bool> is_leader_;
};

class Closure : public braft::Closure {
public:
    Closure() : done(false) {}
    ~Closure() override = default;

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
};

}

#endif
