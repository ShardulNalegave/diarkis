
#include <iostream>
#include <csignal>
#include <memory>
#include <atomic>
#include <chrono>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "CLI/CLI.hpp"

#include "diarkis/events.h"
#include "diarkis/fs_watcher.h"
#include "diarkis/fs_replicator.h"
#include "diarkis/state_machine.h"
#include "diarkis/raft_node.h"

std::atomic<bool> g_running{true};
std::unique_ptr<fs::Watcher> g_watcher;
std::unique_ptr<raft::Node> g_raft_node;
std::unique_ptr<fs::Replicator> g_replicator;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        spdlog::info("Recevied close signal, shutting down...");
        g_running = false;
    }
}

void onRaftApply(const events::Event& event) {
    std::string evt_type = "";
    switch (event.type) {
        case events::EventType::CREATED:
            evt_type = "CREATED";
            break;
        case events::EventType::DELETED:
            evt_type = "DELETED";
            break;
        case events::EventType::MODIFIED:
            evt_type = "MODIFIED";
            break;
        case events::EventType::MOVED:
            evt_type = "MOVED";
            break;
    }

    spdlog::info("[RAFT APPLY EVENT] {}: Item = {}", evt_type, event.relative_path);

    if (g_replicator) {
        if (!g_replicator->applyEvent(event)) {
            spdlog::error("Failed to apply replicated event: {} {}", evt_type, event.relative_path);
        }
    }
}

void onFilesystemEvent(const events::Event& event) {
    std::string evt_type = "";
    switch (event.type) {
        case events::EventType::CREATED:
            evt_type = "CREATED";
            break;
        case events::EventType::DELETED:
            evt_type = "DELETED";
            break;
        case events::EventType::MODIFIED:
            evt_type = "MODIFIED";
            break;
        case events::EventType::MOVED:
            evt_type = "MOVED";
            break;
    }

    spdlog::info("[FS EVENT] {}: Item = {}", evt_type, event.relative_path);
    
    if (g_raft_node && g_raft_node->isLeader()) {
        if (!g_raft_node->proposeEvent(event)) {
            spdlog::warn("Failed to propose event to Raft cluster");
        }
    } else {
        spdlog::debug("Not leader, skipping proposal for: {}", event.relative_path);
    }
}

int main(int argc, char **argv) {
    CLI::App app {"Replicated File-system with Raft consensus", "diarkis"};
    app.set_version_flag("-v,--version", []() { return "diarkis: Version 0.1.0"; });

    int node_id = 1;
    std::string listen_addr;
    std::string peers;
    std::string watch_dir;
    std::string data_dir;

    app.add_option("-i,--id", node_id, "Node ID (unique integer)")
        ->required();

    app.add_option("-a,--address", listen_addr, "Address to use for Raft")
        ->required();
    app.add_option("-p,--peers", peers, "Peer address for Raft")
        ->required();

    app.add_option("-w,--watch", watch_dir, "The path of the directory to replicate")
        ->required()
        ->check(CLI::ExistingDirectory);
    
    app.add_option("-d,--data", data_dir, "Directory to store Raft metadata")
        ->required()
        ->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [node-%n] %v");
    
    auto logger = spdlog::stdout_color_mt(std::to_string(node_id));
    spdlog::set_default_logger(logger);

    g_raft_node = std::make_unique<raft::Node>(node_id, listen_addr, data_dir);
    g_raft_node->setApplyCallback(onRaftApply);
    
    if (!g_raft_node->init(peers)) {
        spdlog::critical("Failed to initialize Raft node");
        return 1;
    }
    
    spdlog::info("Raft node initialized");

    g_watcher = std::make_unique<fs::Watcher>(watch_dir, onFilesystemEvent);    
    if (!g_watcher->start()) {
        spdlog::critical("Failed to start filesystem watcher");
        g_raft_node->shutdown();
        return 1;
    }
    
    spdlog::info("Filesystem watcher started");

    g_replicator = std::make_unique<fs::Replicator>(watch_dir, g_watcher.get());
    spdlog::info("File replicator initialized");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_replicator) {
        g_replicator.reset();
        spdlog::info("File replicator stopped");
    }

    if (g_watcher) {
        g_watcher->stop();
        g_watcher.reset();
    }
    
    if (g_raft_node) {
        g_raft_node->shutdown();
        g_raft_node.reset();
    }
    
    spdlog::info("Shutdown complete");
    return 0;
}
