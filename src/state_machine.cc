
#include "diarkis/state_machine.h"
#include "spdlog/spdlog.h"
#include "msgpack.hpp"
#include "butil/files/file_path.h"
#include "butil/files/file.h"

namespace diarkis {

namespace {
    constexpr size_t MAX_LOG_ENTRY_SIZE = 100 * 1024 * 1024; // 100MB
}

bool StateMachine::Options::validate() const {
    if (base_path.empty()) {
        spdlog::error("base_path is empty");
        return false;
    }
    if (raft_path.empty()) {
        spdlog::error("raft_path is empty");
        return false;
    }
    if (group_id.empty()) {
        spdlog::error("group_id is empty");
        return false;
    }
    if (initial_conf.empty()) {
        spdlog::error("initial_conf is empty");
        return false;
    }
    if (election_timeout_ms <= 0) {
        spdlog::error("Invalid election_timeout_ms: {}", election_timeout_ms);
        return false;
    }
    return true;
}

StateMachine::StateMachine(const Options& opts)
    : options_(opts), is_leader_(false) {
}

StateMachine::~StateMachine() {
    shutdown();
}

Result<void> StateMachine::init() {
    if (!options_.validate()) {
        return Error(ErrorCode::InvalidCommand, "Invalid StateMachine options");
    }
    
    auto result = init_storage();
    if (!result.ok()) return result;
    
    result = init_raft_directories();
    if (!result.ok()) return result;
    
    result = init_brpc_server();
    if (!result.ok()) return result;
    
    result = init_raft_node();
    if (!result.ok()) return result;
    
    spdlog::info("StateMachine initialized - peer: {}, group: {}", 
                 options_.peer_id.to_string(), options_.group_id);
    return Result<void>();
}

Result<void> StateMachine::init_storage() {
    storage_ = std::make_unique<Storage>(options_.base_path);
    return storage_->init();
}

Result<void> StateMachine::init_raft_directories() {
    butil::FilePath raft_path(options_.raft_path);
    butil::FilePath log_path = raft_path.Append("log");
    butil::FilePath meta_path = raft_path.Append("raft_meta");
    butil::FilePath snapshot_path = raft_path.Append("snapshot");

    if (!butil::CreateDirectory(log_path) || 
        !butil::CreateDirectory(meta_path) ||
        !butil::CreateDirectory(snapshot_path)) {
        spdlog::error("Failed to create Raft directories");
        return Error(ErrorCode::IoError, "Failed to create Raft directories");
    }
    
    return Result<void>();
}

Result<void> StateMachine::init_brpc_server() {
    brpc_server_ = std::make_unique<brpc::Server>();
    
    if (braft::add_service(brpc_server_.get(), options_.peer_id.addr) != 0) {
        spdlog::error("Failed to add Raft service to bRPC server");
        return Error(ErrorCode::IoError, "Failed to add Raft service");
    }
    
    if (brpc_server_->Start(options_.peer_id.addr, nullptr) != 0) {
        spdlog::error("Failed to start bRPC server at {}", options_.peer_id.to_string());
        return Error(ErrorCode::NetworkError, "Failed to start bRPC server");
    }
    
    spdlog::info("bRPC server started at {}", options_.peer_id.to_string());
    return Result<void>();
}

Result<void> StateMachine::init_raft_node() {
    butil::FilePath raft_path(options_.raft_path);
    
    braft::NodeOptions node_options;
    if (node_options.initial_conf.parse_from(options_.initial_conf) != 0) {
        spdlog::error("Failed to parse initial configuration: {}", options_.initial_conf);
        return Error(ErrorCode::InvalidCommand, "Invalid initial configuration");
    }

    node_options.election_timeout_ms = options_.election_timeout_ms;
    node_options.fsm = this;
    node_options.node_owns_fsm = false;
    node_options.snapshot_interval_s = options_.snapshot_interval_s;
    node_options.log_uri = "local://" + raft_path.Append("log").value();
    node_options.raft_meta_uri = "local://" + raft_path.Append("raft_meta").value();
    node_options.snapshot_uri = "local://" + raft_path.Append("snapshot").value();

    raft_node_ = std::make_unique<braft::Node>(options_.group_id, options_.peer_id);
    if (raft_node_->init(node_options) != 0) {
        spdlog::error("Failed to initialize Raft node");
        raft_node_.reset();
        return Error(ErrorCode::IoError, "Failed to initialize Raft node");
    }

    spdlog::info("Raft node initialized successfully");
    return Result<void>();
}

void StateMachine::shutdown() {
    if (raft_node_) {
        spdlog::info("Shutting down Raft node...");
        raft_node_->shutdown(nullptr);
        raft_node_->join();
        raft_node_.reset();
    }
    
    if (brpc_server_) {
        spdlog::info("Stopping bRPC server...");
        brpc_server_->Stop(0);
        brpc_server_->Join();
        brpc_server_.reset();
    }
    
    spdlog::info("StateMachine shutdown complete");
}

bool StateMachine::is_leader() const {
    return is_leader_.load(std::memory_order_acquire);
}

braft::PeerId StateMachine::leader_id() const {
    if (!raft_node_) return braft::PeerId();
    return raft_node_->leader_id();
}

commands::Response StateMachine::apply_write_command(const commands::Command& cmd) {
    commands::Response resp;
    
    if (!is_leader()) {
        resp.success = false;
        braft::PeerId leader = leader_id();
        if (leader.is_empty()) {
            resp.error = "No leader available";
        } else {
            resp.error = "Not leader, redirect to: " + leader.to_string();
        }
        return resp;
    }
    
    try {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, cmd);
        
        if (sbuf.size() > MAX_LOG_ENTRY_SIZE) {
            resp.success = false;
            resp.error = "Command too large";
            return resp;
        }
        
        butil::IOBuf log_data;
        log_data.append(sbuf.data(), sbuf.size());
        
        auto closure = std::make_unique<RaftClosure>();
        auto* closure_ptr = closure.get();
        
        braft::Task task;
        task.data = &log_data;
        task.done = closure.release(); // Transfer ownership to Raft
        
        raft_node_->apply(task);
        closure_ptr->wait();
        
        if (closure_ptr->status().ok()) {
            resp.success = true;
        } else {
            resp.success = false;
            resp.error = closure_ptr->status().error_cstr();
        }
        
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("Exception: ") + e.what();
        spdlog::error("Exception applying write command: {}", e.what());
    }
    
    return resp;
}

commands::Response StateMachine::apply_read_command(const commands::Command& cmd) {
    try {
        switch (cmd.type) {
            case commands::Type::READ_FILE:
                return handle_read_file(cmd);
            case commands::Type::LIST_DIR:
                return handle_list_directory(cmd);
            default:
                commands::Response resp;
                resp.success = false;
                resp.error = "Invalid read command type";
                return resp;
        }
    } catch (const std::exception& e) {
        commands::Response resp;
        resp.success = false;
        resp.error = std::string("Read error: ") + e.what();
        spdlog::error("Exception in read command: {}", e.what());
        return resp;
    }
}

commands::Response StateMachine::handle_read_file(const commands::Command& cmd) {
    commands::Response resp;
    auto result = storage_->read_file(cmd.path);
    
    if (result.ok()) {
        resp.success = true;
        resp.data = std::move(result.value());
    } else {
        resp.success = false;
        resp.error = result.error().to_string();
    }
    
    return resp;
}

commands::Response StateMachine::handle_list_directory(const commands::Command& cmd) {
    commands::Response resp;
    auto result = storage_->list_directory(cmd.path);
    
    if (result.ok()) {
        resp.success = true;
        for (const auto& info : result.value()) {
            resp.entries.push_back(info.name);
        }
    } else {
        resp.success = false;
        resp.error = result.error().to_string();
    }
    
    return resp;
}

void StateMachine::on_apply(braft::Iterator& iter) {
    for (; iter.valid(); iter.next()) {
        braft::AsyncClosureGuard closure_guard(iter.done());
        auto* done = dynamic_cast<RaftClosure*>(iter.done());
        
        try {
            std::string data = iter.data().to_string();
            
            if (data.size() > MAX_LOG_ENTRY_SIZE) {
                spdlog::error("Log entry too large: {} bytes", data.size());
                if (done) {
                    done->status().set_error(EINVAL, "Log entry too large");
                }
                continue;
            }
            
            msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
            commands::Command cmd;
            oh.get().convert(cmd);
            
            spdlog::debug("Applying command: type={}, path={}", 
                         static_cast<int>(cmd.type), cmd.path);
            
            apply_command(cmd, done);
            
        } catch (const msgpack::unpack_error& e) {
            spdlog::error("MessagePack unpack error: {}", e.what());
            if (done) {
                done->status().set_error(EINVAL, "Deserialization error");
            }
        } catch (const msgpack::type_error& e) {
            spdlog::error("MessagePack type error: {}", e.what());
            if (done) {
                done->status().set_error(EINVAL, "Type conversion error");
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception applying command: {}", e.what());
            if (done) {
                done->status().set_error(EINVAL, e.what());
            }
        }
    }
}

void StateMachine::apply_command(const commands::Command& cmd, RaftClosure* done) {
    Result<void> result;
    
    switch (cmd.type) {
        case commands::Type::CREATE_FILE:
            result = storage_->create_file(cmd.path);
            break;
            
        case commands::Type::WRITE_FILE:
            result = storage_->write_file(cmd.path, cmd.contents.data(), cmd.contents.size());
            break;
            
        case commands::Type::APPEND_FILE:
            result = storage_->append_file(cmd.path, cmd.contents.data(), cmd.contents.size());
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
            result = storage_->rename(cmd.path, cmd.new_path);
            break;
            
        case commands::Type::READ_FILE:
        case commands::Type::LIST_DIR:
            spdlog::warn("Read-only command in apply: type={}", static_cast<int>(cmd.type));
            return;
            
        default:
            spdlog::error("Unknown command type in apply: {}", static_cast<int>(cmd.type));
            if (done) {
                done->status().set_error(EINVAL, "Unknown command type");
            }
            return;
    }
    
    if (done) {
        if (result.ok()) {
            spdlog::debug("Command applied successfully");
        } else {
            const std::string& error_msg = result.error().to_string();
            done->status().set_error(EINVAL, error_msg.c_str());
            spdlog::error("Command failed: {}", error_msg);
        }
    }
}

void StateMachine::on_shutdown() {
    spdlog::info("StateMachine shutting down");
}

void StateMachine::on_leader_start(int64_t term) {
    is_leader_.store(true, std::memory_order_release);
    spdlog::info("Node became leader at term {}", term);
}

void StateMachine::on_leader_stop(const butil::Status& status) {
    is_leader_.store(false, std::memory_order_release);
    spdlog::info("Node stopped being leader: {}", status.error_cstr());
}

void StateMachine::on_error(const braft::Error& e) {
    spdlog::error("Raft error: type={}, {}", 
                  static_cast<int>(e.type()), e.status().error_cstr());
}

void StateMachine::on_configuration_committed(const braft::Configuration& conf) {
    std::vector<braft::PeerId> peers;
    conf.list_peers(&peers);
    
    std::string conf_str;
    for (size_t i = 0; i < peers.size(); ++i) {
        if (i > 0) conf_str += ",";
        conf_str += peers[i].to_string();
    }
    spdlog::info("Configuration committed: {}", conf_str);
}

void StateMachine::on_start_following(const braft::LeaderChangeContext& ctx) {
    spdlog::info("Started following leader: {}", ctx.leader_id().to_string());
}

void StateMachine::on_stop_following(const braft::LeaderChangeContext& ctx) {
    spdlog::info("Stopped following leader: {}", ctx.leader_id().to_string());
}

void StateMachine::on_snapshot_save(braft::SnapshotWriter* writer, braft::Closure* done) {
    (void)writer;
    spdlog::info("Saving snapshot...");
    // TODO: Implement actual snapshot saving
    if (done) {
        done->Run();
    }
}

int StateMachine::on_snapshot_load(braft::SnapshotReader* reader) {
    (void)reader;
    spdlog::info("Loading snapshot...");
    // TODO: Implement actual snapshot loading
    return 0;
}

}
