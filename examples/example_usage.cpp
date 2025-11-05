#include "diarkis/fs_client.h"
#include <spdlog/spdlog.h>
#include <gflags/gflags.h>
#include <iostream>
#include <thread>
#include <chrono>

DEFINE_string(data_path, "./data", "Base path for filesystem data");
DEFINE_string(raft_path, "./raft", "Path for Raft metadata");
DEFINE_string(group_id, "diarkis_fs", "Raft group ID");
DEFINE_string(peer_id, "127.0.0.1:8100:0", "This peer's ID (ip:port:index)");
DEFINE_string(conf, "127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0", "Initial configuration");
DEFINE_int32(election_timeout, 5000, "Election timeout in milliseconds");

using namespace diarkis;

bool is_valid_leader(const std::string& leader_str) {
    // Check if leader string is empty or represents an invalid/uninitialized peer
    return !leader_str.empty() && 
           leader_str != "0.0.0.0:0:0:0" && 
           leader_str.find("0.0.0.0") == std::string::npos;
}

void wait_for_leader(Client& client) {
    spdlog::info("Waiting for leader election...");
    for (int i = 0; i < 60; ++i) {  // Increased timeout to 30 seconds
        std::string leader = client.get_leader();
        if (is_valid_leader(leader)) {
            spdlog::info("Leader elected: {}", leader);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    spdlog::warn("No leader elected within timeout");
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    // Set log level
    spdlog::set_level(spdlog::level::info);

    // Configure client
    Client::Config config;
    config.data_path = FLAGS_data_path;
    config.raft_path = FLAGS_raft_path;
    config.group_id = FLAGS_group_id;
    config.peer_id = FLAGS_peer_id;
    config.initial_conf = FLAGS_conf;
    config.election_timeout_ms = FLAGS_election_timeout;

    // Initialize client
    Client client(config);
    if (client.init() != 0) {
        spdlog::error("Failed to initialize filesystem client");
        return 1;
    }

    // Wait for leader election
    wait_for_leader(client);

    // Give some time for stabilization
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Example operations
    spdlog::info("Starting example operations...");

    // Check if this node is the leader
    if (client.is_leader()) {
        spdlog::info("This node is the LEADER, performing operations...");
        
        // Create a directory
        int ret = client.create_directory("test_dir");
        if (ret == 0) {
            spdlog::info("✓ Created directory: test_dir");
        } else {
            spdlog::error("✗ Failed to create directory: {}", strerror(ret));
        }

        // Small delay to ensure replication
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create a file
        ret = client.create_file("test_dir/hello.txt");
        if (ret == 0) {
            spdlog::info("✓ Created file: test_dir/hello.txt");
        } else {
            spdlog::error("✗ Failed to create file: {}", strerror(ret));
        }

        // Small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Write to file
        std::string content = "Hello, replicated filesystem!";
        ret = client.write_file("test_dir/hello.txt", content);
        if (ret == 0) {
            spdlog::info("✓ Wrote {} bytes to file: test_dir/hello.txt", content.size());
        } else {
            spdlog::error("✗ Failed to write file: {}", strerror(ret));
        }
        
        spdlog::info("All write operations completed!");
    } else {
        spdlog::info("This node is a FOLLOWER");
        spdlog::info("Current leader: {}", client.get_leader());
    }

    // Wait a bit for replication to all nodes
    spdlog::info("Waiting for replication to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Read file (can be done on any node)
    spdlog::info("Reading file from local storage...");
    std::string read_content;
    ssize_t bytes = client.read_file("test_dir/hello.txt", read_content);
    if (bytes > 0) {
        spdlog::info("✓ Read {} bytes from file", bytes);
        spdlog::info("  Content: \"{}\"", read_content);
    } else if (bytes == -ENOENT) {
        spdlog::warn("File not found (replication may still be in progress)");
    } else {
        spdlog::error("✗ Failed to read file: {}", strerror(-bytes));
    }

    // List directory
    spdlog::info("Listing directory contents...");
    auto entries = client.list_directory("test_dir");
    if (!entries.empty()) {
        spdlog::info("✓ Directory 'test_dir' contains {} entries:", entries.size());
        for (const auto& entry : entries) {
            spdlog::info("    - {}", entry);
        }
    } else {
        spdlog::warn("Directory is empty or does not exist yet");
    }

    // Keep running for a while to observe
    spdlog::info("");
    spdlog::info("=== Filesystem is operational ===");
    spdlog::info("Running for 60 seconds... Press Ctrl+C to exit early");
    std::this_thread::sleep_for(std::chrono::seconds(60));

    // Cleanup
    spdlog::info("Shutting down...");
    client.shutdown();
    google::ShutDownCommandLineFlags();
    
    return 0;
}
