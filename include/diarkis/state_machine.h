
#ifndef DIARKIS_STATE_MACHINE_H
#define DIARKIS_STATE_MACHINE_H

#include <string>
#include <atomic>
#include <functional>
#include <braft/raft.h>

#include "diarkis/events.h"

namespace raft {
    class StateMachine : public braft::StateMachine {
    public:
        StateMachine(std::atomic<bool>* is_leader_, events::EventHandler* callback);

        void on_apply(braft::Iterator& iter) override;
        void on_leader_start(int64_t term) override;
        void on_leader_stop(const butil::Status& status) override;
        void on_error(const braft::Error& e) override;
        void on_shutdown() override;
        void on_stop_following(const braft::LeaderChangeContext& ctx) override;
        void on_start_following(const braft::LeaderChangeContext& ctx) override;

    private:
        int64_t applied_index;
        std::atomic<bool>* is_leader;
        events::EventHandler* apply_callback;
    };

};

#endif /* DIARKIS_STATE_MACHINE_H */
