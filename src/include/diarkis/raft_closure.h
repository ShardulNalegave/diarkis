
#ifndef DIARKIS_RAFT_CLOSURE_H
#define DIARKIS_RAFT_CLOSURE_H

#include "braft/raft.h"
#include <mutex>
#include <condition_variable>

namespace diarkis {

class RaftClosure : public braft::Closure {
public:
    RaftClosure() : done_(false) {}
    ~RaftClosure() override = default;
    
    void Run() override {
        std::unique_lock<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_one();
    }
    
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_;
};

}

#endif
