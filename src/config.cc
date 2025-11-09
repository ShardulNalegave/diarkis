
#include "diarkis/config.h"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"
#include "gflags/gflags.h"
#include <iostream>

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

ServerConfig load_config_from_file(const std::string& config_path) {
    ServerConfig config;
    
    if (config_path.empty()) {
        return config;
    }
    
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);
        
        if (yaml["storage"]) {
            if (yaml["storage"]["base_path"]) {
                config.base_path = yaml["storage"]["base_path"].as<std::string>();
            }
        }
        
        if (yaml["raft"]) {
            if (yaml["raft"]["path"]) {
                config.raft_path = yaml["raft"]["path"].as<std::string>();
            }
            if (yaml["raft"]["group_id"]) {
                config.group_id = yaml["raft"]["group_id"].as<std::string>();
            }
            if (yaml["raft"]["peer_addr"]) {
                config.peer_addr = yaml["raft"]["peer_addr"].as<std::string>();
            }
            if (yaml["raft"]["initial_conf"]) {
                config.initial_conf = yaml["raft"]["initial_conf"].as<std::string>();
            }
            if (yaml["raft"]["election_timeout_ms"]) {
                config.election_timeout_ms = yaml["raft"]["election_timeout_ms"].as<int>();
            }
            if (yaml["raft"]["snapshot_interval"]) {
                config.snapshot_interval = yaml["raft"]["snapshot_interval"].as<int>();
            }
        }
        
        if (yaml["rpc"]) {
            if (yaml["rpc"]["addr"]) {
                config.rpc_addr = yaml["rpc"]["addr"].as<std::string>();
            }
            if (yaml["rpc"]["port"]) {
                config.rpc_port = yaml["rpc"]["port"].as<uint16_t>();
            }
        }
    } catch (const YAML::Exception& e) {
        std::cerr << "Error loading config file: " << e.what() << std::endl;
        std::cerr << "Using default configuration" << std::endl;
    }
    
    return config;
}

void apply_flags_to_config(ServerConfig& config) {
    if (!FLAGS_base_path.empty()) {
        config.base_path = FLAGS_base_path;
    }
    if (!FLAGS_raft_path.empty()) {
        config.raft_path = FLAGS_raft_path;
    }
    if (!FLAGS_group_id.empty()) {
        config.group_id = FLAGS_group_id;
    }
    if (!FLAGS_peer_addr.empty()) {
        config.peer_addr = FLAGS_peer_addr;
    }
    if (!FLAGS_initial_conf.empty()) {
        config.initial_conf = FLAGS_initial_conf;
    }
    if (FLAGS_election_timeout > 0) {
        config.election_timeout_ms = FLAGS_election_timeout;
    }
    if (FLAGS_snapshot_interval > 0) {
        config.snapshot_interval = FLAGS_snapshot_interval;
    }
    if (!FLAGS_rpc_addr.empty()) {
        config.rpc_addr = FLAGS_rpc_addr;
    }
    if (FLAGS_rpc_port > 0) {
        config.rpc_port = static_cast<uint16_t>(FLAGS_rpc_port);
    }
}

}
