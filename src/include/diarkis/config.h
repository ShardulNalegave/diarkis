#ifndef DIARKIS_CONFIG_H
#define DIARKIS_CONFIG_H

#include <cstdint>
#include <string>
#include "diarkis/result.h"

namespace diarkis {

struct ServerConfig {
    // Storage configuration
    std::string base_path = "./data";
    
    // Raft configuration
    std::string raft_path = "./raft";
    std::string group_id = "diarkis_fs";
    std::string peer_addr = "127.0.0.1:8100";
    std::string initial_conf = "127.0.0.1:8100";
    int election_timeout_ms = 5000;
    int snapshot_interval_s = 3600;
    
    // RPC configuration
    std::string rpc_addr = "0.0.0.0";
    uint16_t rpc_port = 9100;
    
    // Validation
    Result<void> validate() const;
};

class ConfigLoader {
public:
    static Result<ServerConfig> load_from_file(const std::string& config_path);
    static void apply_command_line_flags(ServerConfig& config);
    
private:
    static Result<ServerConfig> parse_yaml(const std::string& config_path);
};

}

#endif
