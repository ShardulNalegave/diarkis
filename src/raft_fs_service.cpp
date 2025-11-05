
#include "diarkis/raft_fs_service.h"
#include <spdlog/spdlog.h>
#include <butil/files/file_path.h>
#include <butil/file_util.h>
#include <errno.h>

namespace diarkis {

RaftFilesystemService::RaftFilesystemService(const Options& options)
    : options_(options)
    , is_leader_(false)
    , leader_term_(-1) {
    bthread_mutex_init(&mutex_, nullptr);
    storage_ = std::make_unique<LocalStorageEngine>(options_.data_path);
}

RaftFilesystemService::~RaftFilesystemService() {
    shutdown();
    bthread_mutex_destroy(&mutex_);
}

Result<void> RaftFilesystemService::start() {
    int ret = storage_->initialize();
    if (ret != 0) {
        return Result<void>::Error(FSStatus::IO_ERROR, 
            "Failed to initialize storage: " + std::string(strerror(ret)));
    }

    butil::FilePath raft_path(options_.raft_path);
    butil::FilePath log_path = raft_path.Append("log");
    butil::FilePath meta_path = raft_path.Append("raft_meta");
    butil::FilePath snapshot_path = raft_path.Append("snapshot");

    if (!butil::CreateDirectory(log_path) || 
        !butil::CreateDirectory(meta_path) ||
        !butil::CreateDirectory(snapshot_path)) {
        return Result<void>::Error(FSStatus::IO_ERROR, "Failed to create Raft directories");
    }

    server_ = std::make_unique<brpc::Server>();
    if (braft::add_service(server_.get(), options_.peer_id.addr) != 0) {
        return Result<void>::Error(FSStatus::RAFT_ERROR, "Failed to add Raft service");
    }
    
    if (server_->Start(options_.peer_id.addr, nullptr) != 0) {
        return Result<void>::Error(FSStatus::RAFT_ERROR, 
            "Failed to start brpc server at " + options_.peer_id.to_string());
    }

    braft::NodeOptions node_options;
    if (node_options.initial_conf.parse_from(options_.initial_conf) != 0) {
        return Result<void>::Error(FSStatus::RAFT_ERROR, 
            "Failed to parse initial configuration: " + options_.initial_conf);
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
        return Result<void>::Error(FSStatus::RAFT_ERROR, "Failed to initialize Raft node");
    }

    spdlog::info("Raft filesystem service started - peer: {}, group: {}", 
                 options_.peer_id.to_string(), options_.group_id);
    return Result<void>::Ok();
}

void RaftFilesystemService::shutdown() {
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
    
    spdlog::info("Raft filesystem service shutdown complete");
}

bool RaftFilesystemService::is_leader() const {
    return is_leader_.load(std::memory_order_acquire);
}

braft::PeerId RaftFilesystemService::get_leader() const {
    if (!node_) return braft::PeerId();
    return node_->leader_id();
}

FSStatus RaftFilesystemService::errno_to_status(int err) {
    switch (err) {
        case 0: return FSStatus::OK;
        case ENOENT: return FSStatus::NOT_FOUND;
        case EEXIST: return FSStatus::ALREADY_EXISTS;
        case ENOTDIR: return FSStatus::NOT_DIRECTORY;
        case ENOTEMPTY: return FSStatus::DIRECTORY_NOT_EMPTY;
        case EINVAL: return FSStatus::INVALID_PATH;
        default: return FSStatus::IO_ERROR;
    }
}

Result<void> RaftFilesystemService::submit_operation(const FSOperation& op) {
    if (!is_leader()) {
        braft::PeerId leader = get_leader();
        if (leader.is_empty()) {
            return Result<void>::Error(FSStatus::NO_LEADER, "No leader elected");
        }
        return Result<void>::Error(FSStatus::NOT_LEADER, 
            "Not leader, current leader is: " + leader.to_string());
    }

    std::string serialized = op.serialize();
    butil::IOBuf log;
    log.append(serialized);

    FSOperationClosure* done = new FSOperationClosure();
    
    braft::Task task;
    task.data = &log;
    task.done = done;
    task.expected_term = leader_term_.load(std::memory_order_acquire);

    node_->apply(task);
    done->wait();

    FSStatus status = done->result_status;
    std::string error_msg = done->error_msg;
    delete done;

    if (status != FSStatus::OK) {
        return Result<void>::Error(status, error_msg);
    }
    return Result<void>::Ok();
}

Result<void> RaftFilesystemService::create_file(const std::string& path) {
    FSOperation op(FSOperationType::CREATE_FILE, path);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::write_file(const std::string& path, 
                                                const std::vector<uint8_t>& data) {
    FSOperation op(FSOperationType::WRITE_FILE, path, data);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::append_file(const std::string& path, 
                                                 const std::vector<uint8_t>& data) {
    FSOperation op(FSOperationType::APPEND_FILE, path, data);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::delete_file(const std::string& path) {
    FSOperation op(FSOperationType::DELETE_FILE, path);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::create_directory(const std::string& path) {
    FSOperation op(FSOperationType::CREATE_DIR, path);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::delete_directory(const std::string& path) {
    FSOperation op(FSOperationType::DELETE_DIR, path);
    return submit_operation(op);
}

Result<void> RaftFilesystemService::rename(const std::string& old_path, 
                                            const std::string& new_path) {
    std::vector<uint8_t> new_path_data(new_path.begin(), new_path.end());
    FSOperation op(FSOperationType::RENAME, old_path, new_path_data);
    return submit_operation(op);
}

Result<std::vector<uint8_t>> RaftFilesystemService::read_file(const std::string& path) {
    return storage_->read_file(path);
}

Result<std::vector<std::string>> RaftFilesystemService::list_directory(const std::string& path) {
    return storage_->list_directory(path);
}

Result<FileInfo> RaftFilesystemService::stat(const std::string& path) {
    return storage_->stat(path);
}

Result<bool> RaftFilesystemService::exists(const std::string& path) {
    return storage_->exists(path);
}

void RaftFilesystemService::on_apply(braft::Iterator& iter) {
    for (; iter.valid(); iter.next()) {
        braft::AsyncClosureGuard closure_guard(iter.done());
        
        FSOperationClosure* done = dynamic_cast<FSOperationClosure*>(iter.done());
        
        std::string serialized = iter.data().to_string();
        auto op_opt = FSOperation::deserialize(serialized);
        
        if (!op_opt.has_value()) {
            spdlog::error("Failed to deserialize operation at index {}", iter.index());
            if (done) {
                done->result_status = FSStatus::RAFT_ERROR;
                done->error_msg = "Failed to deserialize operation";
            }
            continue;
        }

        int result = storage_->apply_operation(op_opt.value());
        
        if (done) {
            done->result_status = errno_to_status(result);
            if (result != 0) {
                done->error_msg = strerror(result);
            }
        }
    }
}

void RaftFilesystemService::on_shutdown() {
    spdlog::info("State machine shutting down");
}

void RaftFilesystemService::on_snapshot_save(braft::SnapshotWriter* writer, 
                                              braft::Closure* done) {
    braft::AsyncClosureGuard done_guard(done);
    
    std::string snapshot_path = writer->get_path() + "/data";
    
    if (!butil::CreateDirectory(butil::FilePath(snapshot_path))) {
        done->status().set_error(EIO, "Failed to create snapshot directory");
        return;
    }

    std::string base_path = storage_->get_base_path();
    std::string cmd = "cp -r " + base_path + "/* " + snapshot_path + "/ 2>/dev/null || true";
    if (system(cmd.c_str()) != 0) {
        spdlog::warn("Snapshot save completed with warnings");
    }

    if (writer->add_file("data") != 0) {
        done->status().set_error(EIO, "Failed to add data to snapshot");
        return;
    }

    spdlog::info("Snapshot saved");
}

int RaftFilesystemService::on_snapshot_load(braft::SnapshotReader* reader) {
    std::string snapshot_data_path = reader->get_path() + "/data";
    
    if (!butil::PathExists(butil::FilePath(snapshot_data_path))) {
        spdlog::error("Snapshot data directory not found");
        return -1;
    }

    std::string base_path = storage_->get_base_path();
    std::string cmd = "rm -rf " + base_path + "/* && cp -r " + snapshot_data_path + "/* " + base_path + "/";
    
    if (system(cmd.c_str()) != 0) {
        spdlog::error("Failed to load snapshot");
        return -1;
    }

    spdlog::info("Snapshot loaded successfully");
    return 0;
}

void RaftFilesystemService::on_leader_start(int64_t term) {
    is_leader_.store(true, std::memory_order_release);
    leader_term_.store(term, std::memory_order_release);
    spdlog::info("Node became leader at term {}", term);
}

void RaftFilesystemService::on_leader_stop(const butil::Status& status) {
    is_leader_.store(false, std::memory_order_release);
    spdlog::info("Node stopped being leader: {}", status.error_cstr());
}

void RaftFilesystemService::on_error(const ::braft::Error& e) {
    spdlog::error("Raft error: type={}, {}", static_cast<int>(e.type()), 
                  e.status().error_cstr());
}

void RaftFilesystemService::on_configuration_committed(const ::braft::Configuration& conf) {
    std::vector<braft::PeerId> peers;
    conf.list_peers(&peers);
    std::string conf_str;
    for (size_t i = 0; i < peers.size(); ++i) {
        if (i > 0) conf_str += ",";
        conf_str += peers[i].to_string();
    }
    spdlog::info("Configuration committed: {}", conf_str);
}

void RaftFilesystemService::on_start_following(const ::braft::LeaderChangeContext& ctx) {
    spdlog::info("Started following leader: {}", ctx.leader_id().to_string());
}

void RaftFilesystemService::on_stop_following(const ::braft::LeaderChangeContext& ctx) {
    spdlog::info("Stopped following leader: {}", ctx.leader_id().to_string());
}

} // namespace diarkis
