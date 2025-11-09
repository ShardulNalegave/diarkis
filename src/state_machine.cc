
#include "diarkis/state_machine.h"
#include "diarkis/commands.h"
#include "spdlog/spdlog.h"
#include "msgpack.hpp"

namespace diarkis {

StateMachine::StateMachine(const Options& opts)
    : options_(opts)
    , is_leader_(false) {
    storage_ = std::make_unique<Storage>(options_.base_path);
}

StateMachine::~StateMachine() {
    shutdown();
}

int StateMachine::init() {
    int ret = storage_->init();
    if (ret != 0) {
        return ret;
    }

    butil::FilePath raft_path(options_.raft_path);
    butil::FilePath log_path = raft_path.Append("log");
    butil::FilePath meta_path = raft_path.Append("raft_meta");
    butil::FilePath snapshot_path = raft_path.Append("snapshot");

    if (!butil::CreateDirectory(log_path) || 
        !butil::CreateDirectory(meta_path) ||
        !butil::CreateDirectory(snapshot_path)) {
        spdlog::error("StateMachine::init() : Failed to create Raft directories");
        return -1;
    }

    server_ = std::make_unique<brpc::Server>();
    if (braft::add_service(server_.get(), options_.peer_id.addr) != 0) {
        spdlog::error("StateMachine::init() : Failed to add Raft service");
        return -1;
    }
    
    if (server_->Start(options_.peer_id.addr, nullptr) != 0) {
        spdlog::error("StateMachine::init() : Failed to start bRPC server at {}", options_.peer_id.to_string());
        return -1;
    }

    braft::NodeOptions node_options;
    if (node_options.initial_conf.parse_from(options_.initial_conf) != 0) {
        spdlog::error("StateMachine::init() : Failed to parse initial configuration = {}", options_.initial_conf);
        return -1;
    }

    node_options.election_timeout_ms = options_.election_timeout_ms;
    node_options.fsm = this;
    node_options.node_owns_fsm = false;
    node_options.snapshot_interval_s = options_.snapshot_interval;
    node_options.log_uri = "local://" + log_path.value();
    node_options.raft_meta_uri = "local://" + meta_path.value();
    node_options.snapshot_uri = "local://" + snapshot_path.value();

    node_ = std::make_unique<braft::Node>(options_.group_id, options_.peer_id);
    if (node_->init(node_options) != 0) {
        spdlog::error("StateMachine::init() : Failed to initialize Raft node", options_.initial_conf);
        return -1;
    }

    spdlog::info("Raft State Machine started - peer: {}, group: {}", options_.peer_id.to_string(), options_.group_id);
    return 0;
}

void StateMachine::shutdown() {
    if (node_) {
        node_->shutdown(nullptr);
        node_->join();
        node_.reset();
    }
    
    if (server_) {
        server_->Stop(0);
        server_->Join();
        server_.reset();
    }
    
    spdlog::info("Raft State Machine shutdown complete");
}

bool StateMachine::is_leader() const {
    return is_leader_.load(std::memory_order_acquire);
}

braft::PeerId StateMachine::get_leader() const {
    if (!node_) return braft::PeerId();
    return node_->leader_id();
}

commands::Response StateMachine::apply_write_command(const commands::Command& cmd) {
    commands::Response resp;
    
    if (!is_leader()) {
        resp.success = false;
        braft::PeerId leader = get_leader();
        if (leader.is_empty()) {
            resp.error = "No leader available";
        } else {
            resp.error = "Not leader, redirect to: " + leader.to_string();
        }
        return resp;
    }
    
    msgpack::sbuffer sbuf;
    msgpack::pack(sbuf, cmd);
    
    butil::IOBuf log_data;
    log_data.append(sbuf.data(), sbuf.size());
    
    auto closure = std::make_unique<Closure>();
    braft::Task task;
    task.data = &log_data;
    task.done = closure.get();
    
    node_->apply(task);
    
    closure->wait();
    
    if (closure->status().ok()) {
        resp.success = true;
    } else {
        resp.success = false;
        resp.error = closure->status().error_cstr();
    }
    
    return resp;
}

commands::Response StateMachine::apply_read_command(const commands::Command& cmd) {
    commands::Response resp;
    
    try {
        switch (cmd.type) {
            case commands::Type::READ_FILE: {
                std::string mutable_path = cmd.path;
                std::vector<uint8_t> buffer(10 * 1024 * 1024); // 10MB buffer
                size_t bytes_read = storage_->read_file(mutable_path, buffer.data());
                
                if (bytes_read > 0) {
                    resp.success = true;
                    resp.data.assign(buffer.begin(), buffer.begin() + bytes_read);
                } else {
                    resp.success = false;
                    resp.error = "File not found or read error";
                }
                break;
            }
            
            case commands::Type::LIST_DIR: {
                std::string mutable_path = cmd.path;
                resp.entries = storage_->list_directory(mutable_path);
                resp.success = true;
                break;
            }
            
            default:
                resp.success = false;
                resp.error = "Invalid read command type";
                break;
        }
        
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("Read error: ") + e.what();
    }
    
    return resp;
}

void StateMachine::on_shutdown() {
    spdlog::info("State machine shutting down");
}

void StateMachine::on_leader_start(int64_t term) {
    is_leader_.store(true, std::memory_order_release);
    spdlog::info("Node became leader at term {}", term);
}

void StateMachine::on_leader_stop(const butil::Status& status) {
    is_leader_.store(false, std::memory_order_release);
    spdlog::info("Node stopped being leader: {}", status.error_cstr());
}

void StateMachine::on_error(const ::braft::Error& e)  {
    spdlog::error("Raft error: type={}, {}", static_cast<int>(e.type()), e.status().error_cstr());
}

void StateMachine::on_configuration_committed(const ::braft::Configuration& conf) {
    std::vector<braft::PeerId> peers;
    conf.list_peers(&peers);
    std::string conf_str;
    for (size_t i = 0; i < peers.size(); ++i) {
        if (i > 0) conf_str += ",";
        conf_str += peers[i].to_string();
    }
    spdlog::info("Configuration committed: {}", conf_str);
}

void StateMachine::on_start_following(const ::braft::LeaderChangeContext& ctx) {
    spdlog::info("Started following leader: {}", ctx.leader_id().to_string());
}

void StateMachine::on_stop_following(const ::braft::LeaderChangeContext& ctx) {
    spdlog::info("Stopped following leader: {}", ctx.leader_id().to_string());
}

void StateMachine::on_apply(braft::Iterator& iter) {
    for (; !iter.done(); iter.next()) {
        braft::Closure* done = iter.done();
        
        try {
            auto data = iter.data().to_string();
            msgpack::object_handle oh = msgpack::unpack(data.c_str(), data.size());
            msgpack::object obj = oh.get();

            commands::Command cmd;
            obj.convert(cmd);

            spdlog::debug("Applying command type={} path={}", 
                static_cast<int>(cmd.type), cmd.path);

            int result = 0;
            
            switch (cmd.type) {
                case commands::Type::CREATE_FILE:
                    result = storage_->create_file(cmd.path);
                    break;
                    
                case commands::Type::WRITE_FILE:
                    result = storage_->write_file(cmd.path, 
                        const_cast<uint8_t*>(cmd.contents.data()), 
                        cmd.contents.size());
                    break;
                    
                case commands::Type::APPEND_FILE:
                    result = storage_->append_file(cmd.path, 
                        const_cast<uint8_t*>(cmd.contents.data()), 
                        cmd.contents.size());
                    break;
                    
                case commands::Type::DELETE_FILE:
                    result = storage_->delete_file(cmd.path);
                    break;
                    
                case commands::Type::CREATE_DIR:
                    result = storage_->create_directory(cmd.path);
                    break;
                    
                case commands::Type::DELETE_DIR:
                    result = storage_->delete_directory(cmd.path);
                    break;
                    
                case commands::Type::RENAME:
                    result = storage_->rename_file(cmd.path, cmd.new_path);
                    break;
                    
                case commands::Type::READ_FILE:
                case commands::Type::LIST_DIR:
                    // These are read-only operations, shouldn't go through Raft
                    spdlog::warn("Read-only command in apply: type={}", 
                        static_cast<int>(cmd.type));
                    break;
                    
                default:
                    spdlog::error("Unknown command type in apply: {}", 
                        static_cast<int>(cmd.type));
                    if (done) {
                        done->status().set_error(EINVAL, "Unknown command type");
                    }
                    break;
            }
            
            if (done) {
                if (result == 0) {
                } else {
                    done->status().set_error(result, strerror(result));
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Exception applying command: {}", e.what());
            if (done) {
                done->status().set_error(EINVAL, e.what());
            }
        }
        
        if (done) {
            done->Run();
        }
    }
}

}
