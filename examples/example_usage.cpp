
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
DEFINE_string(conf, "127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0", 
              "Initial cluster configuration");
DEFINE_int32(election_timeout, 5000, "Election timeout in milliseconds");

using namespace diarkis;

bool wait_for_leader(Client& client, int timeout_seconds = 30) {
    spdlog::info("Waiting for leader election...");
    for (int i = 0; i < timeout_seconds * 2; ++i) {
        std::string leader = client.get_leader();
        if (!leader.empty() && leader.find("0.0.0.0") == std::string::npos) {
            spdlog::info("Leader elected: {}", leader);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    spdlog::warn("No leader elected within {} seconds", timeout_seconds);
    return false;
}

template<typename T>
void print_result(const std::string& operation, const Result<T>& result) {
    if (result.ok()) {
        spdlog::info("✓ {}: SUCCESS", operation);
    } else {
        spdlog::error("✗ {}: FAILED - {}", operation, result.error_message);
    }
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    spdlog::set_level(spdlog::level::info);

    Client::Config config;
    config.data_path = FLAGS_data_path;
    config.raft_path = FLAGS_raft_path;
    config.group_id = FLAGS_group_id;
    config.peer_id = FLAGS_peer_id;
    config.initial_conf = FLAGS_conf;
    config.election_timeout_ms = FLAGS_election_timeout;

    Client client(config);
    
    auto init_result = client.init();
    if (!init_result.ok()) {
        spdlog::error("Failed to initialize client: {}", init_result.error_message);
        return 1;
    }

    if (!wait_for_leader(client)) {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    spdlog::info("");
    spdlog::info("=== Starting Filesystem Operations ===");
    spdlog::info("Node: {}", FLAGS_peer_id);
    spdlog::info("Role: {}", client.is_leader() ? "LEADER" : "FOLLOWER");
    spdlog::info("Leader: {}", client.get_leader());
    spdlog::info("");

    // ==================== Write Operations (Leader Only) ====================
    
    if (client.is_leader()) {
        spdlog::info("--- Write Operations (Leader) ---");
        
        auto result = client.create_directory("projects");
        print_result("Create directory 'projects'", result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result = client.create_file("projects/README.md");
        print_result("Create file 'projects/README.md'", result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string readme_content = 
            "# Distributed Filesystem\n\n"
            "This is a replicated filesystem using Raft consensus.\n"
            "All writes go through the leader and are replicated to followers.\n";
        
        result = client.write_file("projects/README.md", readme_content);
        print_result("Write to 'projects/README.md'", result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result = client.create_file("projects/status.txt");
        print_result("Create file 'projects/status.txt'", result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result = client.write_file("projects/status.txt", "Initial status: Online\n");
        print_result("Write to 'projects/status.txt'", result);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        result = client.append_file("projects/status.txt", "Update: All systems operational\n");
        print_result("Append to 'projects/status.txt'", result);
        
        spdlog::info("All write operations completed!");
        spdlog::info("");
    } else {
        spdlog::info("--- Skipping Writes (Not Leader) ---");
        spdlog::info("Only the leader can perform write operations");
        spdlog::info("");
    }

    // Wait for replication
    spdlog::info("Waiting for replication to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    spdlog::info("--- Read Operations (Can run on any node) ---");

    auto list_result = client.list_directory("projects");
    if (list_result.ok()) {
        spdlog::info("✓ Directory 'projects' contains {} entries:", list_result.value.size());
        for (const auto& entry : list_result.value) {
            spdlog::info("    - {}", entry);
        }
    } else {
        spdlog::error("✗ Failed to list directory: {}", list_result.error_message);
    }

    spdlog::info("");

    auto read_result = client.read_file_string("projects/README.md");
    if (read_result.ok()) {
        spdlog::info("✓ Read 'projects/README.md' ({} bytes)", read_result.value.size());
        spdlog::info("Content preview:");
        spdlog::info("---");
        spdlog::info("{}", read_result.value);
        spdlog::info("---");
    } else {
        spdlog::error("✗ Failed to read file: {}", read_result.error_message);
    }

    spdlog::info("");

    read_result = client.read_file_string("projects/status.txt");
    if (read_result.ok()) {
        spdlog::info("✓ Read 'projects/status.txt' ({} bytes)", read_result.value.size());
        spdlog::info("Content:");
        spdlog::info("{}", read_result.value);
    } else {
        spdlog::error("✗ Failed to read file: {}", read_result.error_message);
    }

    spdlog::info("");

    auto exists_result = client.exists("projects/README.md");
    if (exists_result.ok()) {
        spdlog::info("✓ File 'projects/README.md' exists: {}", 
                     exists_result.value ? "YES" : "NO");
    }

    read_result = client.read_file_string("projects/missing.txt");
    if (!read_result.ok()) {
        spdlog::info("✓ Correctly detected missing file: {}", read_result.error_message);
    }

    spdlog::info("Press Ctrl+C to exit");
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    client.shutdown();
    google::ShutDownCommandLineFlags();
    
    return 0;
}
