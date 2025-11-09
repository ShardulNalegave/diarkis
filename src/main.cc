
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>

#include "spdlog/spdlog.h"
#include "gflags/gflags.h"

#include "diarkis/state_machine.h"
#include "diarkis/rpc.h"
#include "diarkis/config.h"

DEFINE_string(config, "", "Path to YAML configuration file");

namespace {
    std::atomic<bool> g_running{true};
    std::shared_ptr<diarkis::StateMachine> g_state_machine;
    std::shared_ptr<diarkis::RpcServer> g_rpc_server;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        spdlog::info("Received signal {}, shutting down...", signum);
        g_running.store(false);
    }
}

void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);
}

bool initialize_server(const diarkis::ServerConfig& config) {
    try {
        braft::PeerId peer_id;
        if (peer_id.parse(config.peer_addr) != 0) {
            spdlog::error("Failed to parse peer address: {}", config.peer_addr);
            return false;
        }
        
        diarkis::StateMachine::Options sm_opts;
        sm_opts.base_path = config.base_path;
        sm_opts.raft_path = config.raft_path;
        sm_opts.group_id = config.group_id;
        sm_opts.peer_id = peer_id;
        sm_opts.initial_conf = config.initial_conf;
        sm_opts.election_timeout_ms = config.election_timeout_ms;
        sm_opts.snapshot_interval = config.snapshot_interval;
        
        spdlog::info("Initializing state machine...");
        g_state_machine = std::make_shared<diarkis::StateMachine>(sm_opts);
        
        if (g_state_machine->init() != 0) {
            spdlog::error("Failed to initialize state machine");
            return false;
        }
        
        spdlog::info("State machine initialized successfully");
        
        spdlog::info("Initializing RPC server...");
        g_rpc_server = std::make_shared<diarkis::RpcServer>(
            config.rpc_addr, config.rpc_port, g_state_machine);
        
        if (!g_rpc_server->start()) {
            spdlog::error("Failed to start RPC server");
            return false;
        }
        
        spdlog::info("RPC server started successfully on {}:{}", 
            config.rpc_addr, config.rpc_port);
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during initialization: {}", e.what());
        return false;
    }
}

void shutdown_server() {
    spdlog::info("Shutting down server components...");
    
    if (g_rpc_server) {
        spdlog::info("Stopping RPC server...");
        g_rpc_server->stop();
        g_rpc_server.reset();
        spdlog::info("RPC server stopped");
    }
    
    if (g_state_machine) {
        spdlog::info("Shutting down state machine...");
        g_state_machine->shutdown();
        g_state_machine.reset();
        spdlog::info("State machine shutdown complete");
    }
    
    spdlog::info("Server shutdown complete");
}

int main(int argc, char* argv[]) {
    gflags::SetUsageMessage("Diarkis Replicated Filesystem Server");
    gflags::SetVersionString("0.1.0");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    
    diarkis::ServerConfig config;
    if (!FLAGS_config.empty()) {
        config = diarkis::load_config_from_file(FLAGS_config);
    }
    
    diarkis::apply_flags_to_config(config);
    
    setup_signal_handlers();
    
    if (!initialize_server(config)) {
        spdlog::error("Failed to initialize server");
        gflags::ShutDownCommandLineFlags();
        return 1;
    }
    
    spdlog::info("Server started successfully");
    spdlog::info("Press Ctrl+C to stop");
    
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    shutdown_server();
    
    gflags::ShutDownCommandLineFlags();
    spdlog::info("Goodbye!");
    return 0;
}
