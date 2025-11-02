#include "diarkis/raft_node.h"
#include "diarkis/state_machine.h"

#include <unistd.h>
#include <spdlog/spdlog.h>

namespace raft {

Node::Node(int node_id_, const std::string& listen_addr_, const std::string& data_dir_)
    : node_id(node_id_), listen_addr(listen_addr_), data_dir(data_dir_),
        node(nullptr), state_machine(nullptr), server(nullptr), is_leader(false) {
    if (data_dir[data_dir.length() - 1] != '/') {
        data_dir += '/';
    }
}

Node::~Node() {
    shutdown();
}

bool Node::init(const std::string& peers) {
    size_t colon_pos = listen_addr.find(':');
    if (colon_pos == std::string::npos) {
        spdlog::error("Invalid listen address format: {}", listen_addr);
        return false;
    }
    int port = std::stoi(listen_addr.substr(colon_pos + 1));
    
    server = new brpc::Server();
    braft::add_service(server, listen_addr.c_str());

    if (server->Start(port, nullptr) != 0) {
        spdlog::error("Failed to start brpc server on port {}", port);
        delete server;
        server = nullptr;
        return false;
    }

    spdlog::info("brpc server started on port {}", port);

    state_machine = new StateMachine(&is_leader, &apply_callback);

    braft::NodeOptions node_options;
    node_options.fsm = state_machine;
    node_options.node_owns_fsm = false;

    node_options.election_timeout_ms = 5000;
    node_options.snapshot_interval_s = 3600;

    std::string prefix = data_dir + "node_" + std::to_string(node_id);
    node_options.log_uri = "local://" + prefix + "/log";
    node_options.raft_meta_uri = "local://" + prefix + "/raft_meta";
    node_options.snapshot_uri = "local://" + prefix + "/snapshot";

    mkdir(prefix.c_str(), 0755);
    mkdir((prefix + "/log").c_str(), 0755);
    mkdir((prefix + "/raft_meta").c_str(), 0755);
    mkdir((prefix + "/snapshot").c_str(), 0755);

    braft::Configuration initial_conf;
    std::istringstream iss(peers);
    std::string peer;
    while (std::getline(iss, peer, ',')) {
        peer.erase(0, peer.find_first_not_of(" \t"));
        peer.erase(peer.find_last_not_of(" \t") + 1);
        
        braft::PeerId peer_id;
        if (peer_id.parse(peer) != 0) {
            spdlog::error("Failed to parse peer: {}", peer);
            delete state_machine;
            return false;
        }
        initial_conf.add_peer(peer_id);
    }
    node_options.initial_conf = initial_conf;

    braft::PeerId peer_id;
    if (peer_id.parse(listen_addr) != 0) {
        spdlog::error("Failed to parse listen address: {}", listen_addr);
        delete state_machine;
        return false;
    }

    node = new braft::Node(DIARKIS_RAFT_GROUP_ID, peer_id);

    if (node->init(node_options) != 0) {
        spdlog::error("Failed to init Raft node");
        delete node;
        delete state_machine;
        node = nullptr;
        state_machine = nullptr;
        return false;
    }
    
    spdlog::info("Raft node initialized");
    spdlog::info("  Node ID: {}", node_id);
    spdlog::info("  Listen: {}", listen_addr);
    return true;
}

void Node::shutdown() {
    if (node) {
        node->shutdown(nullptr);
        node->join();
        delete node;
        node = nullptr;
    }
    
    if (state_machine) {
        delete state_machine;
        state_machine = nullptr;
    }

    if (server) {
        server->Stop(0);
        server->Join();
        delete server;
        server = nullptr;
    }
}

bool Node::proposeEvent(const events::Event& event) {
    if (!isLeader()) {
        spdlog::warn("Not leader, cannot propose event");
        return false;
    }
    
    auto [serialized, size] = event.serialize();
    
    butil::IOBuf buf;
    buf.append(serialized, size);
    
    braft::Task task;
    task.data = &buf;
    task.done = nullptr;
    
    node->apply(task);
    
    spdlog::debug("Proposed event: type={}, relative_path={}, size={} bytes", 
        static_cast<int>(event.type), event.relative_path, size);
    
    return true;
}

std::string Node::getLeaderAddr() const {
    if (!node) {
        return "";
    }
    
    braft::PeerId leader = node->leader_id();
    if (leader.is_empty()) {
        return "";
    }
    
    return leader.to_string();
}


void Node::setApplyCallback(events::EventHandler callback) {
    apply_callback = std::move(callback);
}

bool Node::isLeader() const {
    return is_leader.load(std::memory_order_acquire);
}

};