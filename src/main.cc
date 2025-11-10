
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

#include "spdlog/spdlog.h"
#include "gflags/gflags.h"

#include "diarkis/state_machine.h"
#include "diarkis/rpc.h"
#include "diarkis/config.h"

DEFINE_string(config, "", "Path to YAML configuration file");
DEFINE_string(log_level, "info", "Log level (trace, debug, info, warn, error, critical)");

namespace {
    std::atomic<bool> g_running{true};
    std::shared_ptr<diarkis::StateMachine> g_state_machine;
    std::shared_ptr<diarkis::RpcServer> g_rpc_server;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        spdlog::info("Received signal {}, initiating shutdown...", signum);
        g_running.store(false, std::memory_order_release);
    }
}

void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);
}

void setup_logging(const std::string& level) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    
    // Set log level
    if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else if (level == "critical") {
        spdlog::set_level(spdlog::level::critical);
    } else {
        spdlog::warn("Unknown log level '{}', using 'info'", level);
        spdlog::set_level(spdlog::level::info);
    }
}

diarkis::Result<diarkis::ServerConfig> load_configuration() {
    auto result = diarkis::ConfigLoader::load_from_file(FLAGS_config);
    if (!result.ok()) {
        return result;
    }
    
    diarkis::ServerConfig config = result.value();
    diarkis::ConfigLoader::apply_command_line_flags(config);
    
    auto validation = config.validate();
    if (!validation.ok()) {
        return diarkis::Error(diarkis::ErrorCode::InvalidCommand,
                            "Configuration validation failed: " + validation.error().to_string());
    }
    
    return config;
}

diarkis::Result<void> initialize_state_machine(const diarkis::ServerConfig& config) {
    spdlog::info("Initializing state machine...");
    
    braft::PeerId peer_id;
    if (peer_id.parse(config.peer_addr) != 0) {
        return diarkis::Error(diarkis::ErrorCode::InvalidCommand,
                            "Failed to parse peer address: " + config.peer_addr);
    }
    
    diarkis::StateMachine::Options sm_opts;
    sm_opts.base_path = config.base_path;
    sm_opts.raft_path = config.raft_path;
    sm_opts.group_id = config.group_id;
    sm_opts.peer_id = peer_id;
    sm_opts.initial_conf = config.initial_conf;
    sm_opts.election_timeout_ms = config.election_timeout_ms;
    sm_opts.snapshot_interval_s = config.snapshot_interval_s;
    
    g_state_machine = std::make_shared<diarkis::StateMachine>(sm_opts);
    
    auto result = g_state_machine->init();
    if (!result.ok()) {
        g_state_machine.reset();
        return result;
    }
    
    spdlog::info("State machine initialized successfully");
    return diarkis::Result<void>();
}

diarkis::Result<void> initialize_rpc_server(const diarkis::ServerConfig& config) {
    spdlog::info("Initializing RPC server...");
    
    g_rpc_server = std::make_shared<diarkis::RpcServer>(
        config.rpc_addr, config.rpc_port, g_state_machine);
    
    if (!g_rpc_server->start()) {
        g_rpc_server.reset();
        return diarkis::Error(diarkis::ErrorCode::NetworkError,
                            "Failed to start RPC server");
    }
    
    spdlog::info("RPC server started on {}:{}", config.rpc_addr, config.rpc_port);
    return diarkis::Result<void>();
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

    setup_logging(FLAGS_log_level);
    setup_signal_handlers();
    
    spdlog::info("=== Diarkis Server Starting ===");
    
    // Load configuration
    auto config_result = load_configuration();
    if (!config_result.ok()) {
        spdlog::error("Configuration error: {}", config_result.error().to_string());
        gflags::ShutDownCommandLineFlags();
        return 1;
    }
    
    const auto& config = config_result.value();
    spdlog::info("Configuration loaded successfully");
    spdlog::info("  Base path: {}", config.base_path);
    spdlog::info("  Raft path: {}", config.raft_path);
    spdlog::info("  Group ID: {}", config.group_id);
    spdlog::info("  Peer address: {}", config.peer_addr);
    spdlog::info("  RPC address: {}:{}", config.rpc_addr, config.rpc_port);
    
    // Initialize state machine
    auto sm_result = initialize_state_machine(config);
    if (!sm_result.ok()) {
        spdlog::error("Failed to initialize state machine: {}", 
                     sm_result.error().to_string());
        gflags::ShutDownCommandLineFlags();
        return 1;
    }
    
    // Initialize RPC server
    auto rpc_result = initialize_rpc_server(config);
    if (!rpc_result.ok()) {
        spdlog::error("Failed to initialize RPC server: {}", 
                     rpc_result.error().to_string());
        shutdown_server();
        gflags::ShutDownCommandLineFlags();
        return 1;
    }
    
    spdlog::info("=== Server started successfully ===");
    spdlog::info("Press Ctrl+C to stop");
    
    // Main loop
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    spdlog::info("=== Shutting down ===");
    shutdown_server();
    
    gflags::ShutDownCommandLineFlags();
    spdlog::info("Goodbye!");
    return 0;
}
