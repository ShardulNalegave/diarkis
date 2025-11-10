
#include "diarkis/config.h"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"
#include "gflags/gflags.h"

DEFINE_string(base_path, "", "Storage base path");
DEFINE_string(raft_path, "", "Raft data path");
DEFINE_string(group_id, "", "Raft group ID");
DEFINE_string(peer_addr, "", "Raft peer address (IP:PORT)");
DEFINE_string(initial_conf, "", "Raft initial configuration (comma-separated peers)");
DEFINE_int32(election_timeout, 0, "Raft election timeout in milliseconds");
DEFINE_int32(snapshot_interval, 0, "Raft snapshot interval in seconds");
DEFINE_string(rpc_addr, "", "RPC bind address");
DEFINE_int32(rpc_port, 0, "RPC bind port");

namespace diarkis {

Result<void> ServerConfig::validate() const {
    if (base_path.empty()) {
        return Error(ErrorCode::InvalidCommand, "base_path cannot be empty");
    }
    if (raft_path.empty()) {
        return Error(ErrorCode::InvalidCommand, "raft_path cannot be empty");
    }
    if (group_id.empty()) {
        return Error(ErrorCode::InvalidCommand, "group_id cannot be empty");
    }
    if (peer_addr.empty()) {
        return Error(ErrorCode::InvalidCommand, "peer_addr cannot be empty");
    }
    if (initial_conf.empty()) {
        return Error(ErrorCode::InvalidCommand, "initial_conf cannot be empty");
    }
    if (election_timeout_ms <= 0) {
        return Error(ErrorCode::InvalidCommand, "election_timeout_ms must be positive");
    }
    if (snapshot_interval_s < 0) {
        return Error(ErrorCode::InvalidCommand, "snapshot_interval_s cannot be negative");
    }
    if (rpc_addr.empty()) {
        return Error(ErrorCode::InvalidCommand, "rpc_addr cannot be empty");
    }
    if (rpc_port == 0) {
        return Error(ErrorCode::InvalidCommand, "rpc_port must be specified");
    }
    return Result<void>();
}

Result<ServerConfig> ConfigLoader::load_from_file(const std::string& config_path) {
    if (config_path.empty()) {
        return ServerConfig(); // Return default config
    }
    
    return parse_yaml(config_path);
}

Result<ServerConfig> ConfigLoader::parse_yaml(const std::string& config_path) {
    ServerConfig config;
    
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);
        
        // Parse storage section
        if (yaml["storage"]) {
            const auto& storage = yaml["storage"];
            if (storage["base_path"]) {
                config.base_path = storage["base_path"].as<std::string>();
            }
        }
        
        // Parse Raft section
        if (yaml["raft"]) {
            const auto& raft = yaml["raft"];
            if (raft["path"]) {
                config.raft_path = raft["path"].as<std::string>();
            }
            if (raft["group_id"]) {
                config.group_id = raft["group_id"].as<std::string>();
            }
            if (raft["peer_addr"]) {
                config.peer_addr = raft["peer_addr"].as<std::string>();
            }
            if (raft["initial_conf"]) {
                config.initial_conf = raft["initial_conf"].as<std::string>();
            }
            if (raft["election_timeout_ms"]) {
                config.election_timeout_ms = raft["election_timeout_ms"].as<int>();
            }
            if (raft["snapshot_interval"]) {
                config.snapshot_interval_s = raft["snapshot_interval"].as<int>();
            }
        }
        
        // Parse RPC section
        if (yaml["rpc"]) {
            const auto& rpc = yaml["rpc"];
            if (rpc["addr"]) {
                config.rpc_addr = rpc["addr"].as<std::string>();
            }
            if (rpc["port"]) {
                config.rpc_port = rpc["port"].as<uint16_t>();
            }
        }
        
        spdlog::info("Loaded configuration from {}", config_path);
        return config;
        
    } catch (const YAML::Exception& e) {
        spdlog::error("Error loading config file {}: {}", config_path, e.what());
        return Error(ErrorCode::IoError, 
                    std::string("Failed to parse config: ") + e.what());
    }
}

void ConfigLoader::apply_command_line_flags(ServerConfig& config) {
    if (!FLAGS_base_path.empty()) {
        config.base_path = FLAGS_base_path;
        spdlog::debug("Override base_path: {}", config.base_path);
    }
    if (!FLAGS_raft_path.empty()) {
        config.raft_path = FLAGS_raft_path;
        spdlog::debug("Override raft_path: {}", config.raft_path);
    }
    if (!FLAGS_group_id.empty()) {
        config.group_id = FLAGS_group_id;
        spdlog::debug("Override group_id: {}", config.group_id);
    }
    if (!FLAGS_peer_addr.empty()) {
        config.peer_addr = FLAGS_peer_addr;
        spdlog::debug("Override peer_addr: {}", config.peer_addr);
    }
    if (!FLAGS_initial_conf.empty()) {
        config.initial_conf = FLAGS_initial_conf;
        spdlog::debug("Override initial_conf: {}", config.initial_conf);
    }
    if (FLAGS_election_timeout > 0) {
        config.election_timeout_ms = FLAGS_election_timeout;
        spdlog::debug("Override election_timeout_ms: {}", config.election_timeout_ms);
    }
    if (FLAGS_snapshot_interval > 0) {
        config.snapshot_interval_s = FLAGS_snapshot_interval;
        spdlog::debug("Override snapshot_interval_s: {}", config.snapshot_interval_s);
    }
    if (!FLAGS_rpc_addr.empty()) {
        config.rpc_addr = FLAGS_rpc_addr;
        spdlog::debug("Override rpc_addr: {}", config.rpc_addr);
    }
    if (FLAGS_rpc_port > 0) {
        config.rpc_port = static_cast<uint16_t>(FLAGS_rpc_port);
        spdlog::debug("Override rpc_port: {}", config.rpc_port);
    }
}

}
