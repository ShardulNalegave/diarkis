
#include "diarkis/fs_client.h"
#include "diarkis/raft_fs_service.h"
#include <spdlog/spdlog.h>

namespace diarkis {

Client::Client(const Config& config)
    : config_(config) {
}

Client::~Client() {
    shutdown();
}

Result<void> Client::init() {
    braft::PeerId peer_id;
    if (peer_id.parse(config_.peer_id) != 0) {
        return Result<void>::Error(FSStatus::INVALID_PATH, 
            "Invalid peer_id format: " + config_.peer_id);
    }

    RaftFilesystemService::Options options;
    options.data_path = config_.data_path;
    options.raft_path = config_.raft_path;
    options.group_id = config_.group_id;
    options.peer_id = peer_id;
    options.initial_conf = config_.initial_conf;
    options.election_timeout_ms = config_.election_timeout_ms;
    options.snapshot_interval = config_.snapshot_interval;

    service_ = std::make_unique<RaftFilesystemService>(options);
    auto result = service_->start();
    
    if (!result.ok()) {
        service_.reset();
        return result;
    }

    spdlog::info("Filesystem client initialized");
    return Result<void>::Ok();
}

void Client::shutdown() {
    if (service_) {
        service_->shutdown();
        service_.reset();
    }
}

Result<void> Client::create_file(const std::string& path) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->create_file(path);
}

Result<void> Client::write_file(const std::string& path, const std::vector<uint8_t>& data) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->write_file(path, data);
}

Result<void> Client::write_file(const std::string& path, const std::string& data) {
    std::vector<uint8_t> buffer(data.begin(), data.end());
    return write_file(path, buffer);
}

Result<void> Client::append_file(const std::string& path, const std::vector<uint8_t>& data) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->append_file(path, data);
}

Result<void> Client::append_file(const std::string& path, const std::string& data) {
    std::vector<uint8_t> buffer(data.begin(), data.end());
    return append_file(path, buffer);
}

Result<void> Client::delete_file(const std::string& path) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->delete_file(path);
}

Result<void> Client::create_directory(const std::string& path) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->create_directory(path);
}

Result<void> Client::delete_directory(const std::string& path) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->delete_directory(path);
}

Result<void> Client::rename(const std::string& old_path, const std::string& new_path) {
    if (!service_) return Result<void>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->rename(old_path, new_path);
}

Result<std::vector<uint8_t>> Client::read_file(const std::string& path) {
    if (!service_) return Result<std::vector<uint8_t>>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->read_file(path);
}

Result<std::string> Client::read_file_string(const std::string& path) {
    auto result = read_file(path);
    if (!result.ok()) {
        return Result<std::string>::Error(result.status, result.error_message);
    }
    
    std::string content(result.value.begin(), result.value.end());
    return Result<std::string>::Ok(std::move(content));
}

Result<std::vector<std::string>> Client::list_directory(const std::string& path) {
    if (!service_) return Result<std::vector<std::string>>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->list_directory(path);
}

Result<FileInfo> Client::stat(const std::string& path) {
    if (!service_) return Result<FileInfo>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->stat(path);
}

Result<bool> Client::exists(const std::string& path) {
    if (!service_) return Result<bool>::Error(FSStatus::IO_ERROR, "Service not initialized");
    return service_->exists(path);
}

bool Client::is_leader() const {
    if (!service_) return false;
    return service_->is_leader();
}

std::string Client::get_leader() const {
    if (!service_) return "";
    return service_->get_leader().to_string();
}

} // namespace diarkis
