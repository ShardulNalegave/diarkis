
#ifndef DIARKIS_CONFIG_H
#define DIARKIS_CONFIG_H

#include "stdint.h"
#include <string>
#include <cstddef>

namespace diarkis {

struct ServerConfig {
    std::string base_path = "./data";
    
    std::string raft_path = "./raft";
    std::string group_id = "diarkis_fs";
    std::string peer_addr = "127.0.0.1:8100";
    std::string initial_conf = "127.0.0.1:8100";
    int election_timeout_ms = 5000;
    int snapshot_interval = 3600;
    
    std::string rpc_addr = "0.0.0.0";
    uint16_t rpc_port = 9100;
};

ServerConfig load_config_from_file(const std::string& config_path);
void apply_flags_to_config(ServerConfig& config);

}

#endif
