
#include "diarkis/fs_client.h"
#include "diarkis/raft_fs_service.h"
#include <spdlog/spdlog.h>
#include <errno.h>

namespace diarkis {

Client::Client(const Config& config)
    : config_(config) {
}

Client::~Client() {
    shutdown();
}

int Client::init() {
    // Parse peer ID
    braft::PeerId peer_id;
    if (peer_id.parse(config_.peer_id) != 0) {
        spdlog::error("Invalid peer_id format: {}", config_.peer_id);
        return -1;
    }

    // Create service options
    RaftFilesystemService::Options options;
    options.data_path = config_.data_path;
    options.raft_path = config_.raft_path;
    options.group_id = config_.group_id;
    options.peer_id = peer_id;
    options.initial_conf = config_.initial_conf;
    options.election_timeout_ms = config_.election_timeout_ms;
    options.snapshot_interval = config_.snapshot_interval;

    // Create and start service
    service_ = std::make_unique<RaftFilesystemService>(options);
    int ret = service_->start();
    
    if (ret != 0) {
        spdlog::error("Failed to start filesystem service");
        service_.reset();
        return ret;
    }

    spdlog::info("Filesystem client initialized");
    return 0;
}

void Client::shutdown() {
    if (service_) {
        service_->shutdown();
        service_.reset();
    }
}

int Client::create_file(const std::string& path) {
    if (!service_) return EINVAL;
    return service_->create_file(path);
}

int Client::write_file(const std::string& path, const std::vector<uint8_t>& data) {
    if (!service_) return EINVAL;
    return service_->write_file(path, data);
}

int Client::write_file(const std::string& path, const std::string& data) {
    std::vector<uint8_t> buffer(data.begin(), data.end());
    return write_file(path, buffer);
}

int Client::delete_file(const std::string& path) {
    if (!service_) return EINVAL;
    return service_->delete_file(path);
}

int Client::create_directory(const std::string& path) {
    if (!service_) return EINVAL;
    return service_->create_directory(path);
}

int Client::delete_directory(const std::string& path) {
    if (!service_) return EINVAL;
    return service_->delete_directory(path);
}

int Client::rename(const std::string& old_path, const std::string& new_path) {
    if (!service_) return EINVAL;
    return service_->rename(old_path, new_path);
}

ssize_t Client::read_file(const std::string& path, std::vector<uint8_t>& buffer) {
    if (!service_) return -EINVAL;
    return service_->read_file(path, buffer);
}

ssize_t Client::read_file(const std::string& path, std::string& content) {
    if (!service_) return -EINVAL;
    std::vector<uint8_t> buffer;
    ssize_t ret = service_->read_file(path, buffer);
    if (ret > 0) {
        content.assign(buffer.begin(), buffer.end());
    }
    return ret;
}

std::vector<std::string> Client::list_directory(const std::string& path) {
    if (!service_) return {};
    return service_->list_directory(path);
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
