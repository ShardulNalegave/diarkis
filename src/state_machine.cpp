#include "diarkis/state_machine.h"

#include <braft/util.h>
#include <spdlog/spdlog.h>

namespace raft {

StateMachine::StateMachine(std::atomic<bool>* is_leader_, events::EventHandler* callback_)
    : apply_callback(callback_), is_leader(is_leader_), applied_index(0) {
    //
}

void StateMachine::on_apply(braft::Iterator& iter) {
    for (; iter.valid(); iter.next()) {
        if (iter.done()) {
            iter.done()->Run();
            continue;
        }
        
        butil::IOBuf data = iter.data();
        std::string data_str = data.to_string();
        
        events::Event event = events::Event::deserialize(data_str.c_str(), data_str.size());
        
        if (event.type == events::EventType::INVALID) {
            spdlog::error("Failed to deserialize event at index {}", iter.index());
            continue;
        }
        
        spdlog::info("Applying op at index {}: type={}, relative_path={}", 
            iter.index(), static_cast<int>(event.type), event.relative_path);
        
        if (apply_callback && *apply_callback) {
            (*apply_callback)(event);
        }
        
        applied_index = iter.index();
    }
}

void StateMachine::on_leader_start(int64_t term) {
    spdlog::info("🎖️  Became LEADER at term {}", term);
    is_leader->store(true, std::memory_order_release);
}

void StateMachine::on_leader_stop(const butil::Status& status) {
    spdlog::info("👋 Lost leadership: {}", status.error_cstr());
    is_leader->store(false, std::memory_order_release);
}

void StateMachine::on_shutdown() {
    spdlog::info("State machine shutting down");
}

void StateMachine::on_error(const braft::Error& e) {
    spdlog::error("Raft error: {}", e.status().error_cstr());
}

void StateMachine::on_stop_following(const braft::LeaderChangeContext& ctx) {
    spdlog::info("Stopped following {}", ctx.leader_id().to_string().c_str());
}

void StateMachine::on_start_following(const braft::LeaderChangeContext& ctx) {
    spdlog::info("Started following {}", ctx.leader_id().to_string().c_str());
}

};