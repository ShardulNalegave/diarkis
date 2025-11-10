
#include "diarkis_client/rpc.h"
#include "spdlog/spdlog.h"
#include "arpa/inet.h"
#include "msgpack.hpp"

namespace diarkis_client {

RpcClient::RpcClient(const std::string& address, uint16_t port)
    : address_(address), port_(port) {
}

RpcClient::~RpcClient() {
    disconnect();
}

bool RpcClient::connect() {
    if (conn_ && conn_->socket_fd() >= 0) {
        spdlog::debug("Already connected to {}:{}", address_, port_);
        return true;
    }
    
    conn_ = std::make_unique<TcpConnection>(address_, port_);
    
    if (conn_->socket_fd() < 0) {
        spdlog::error("Failed to connect to {}:{}", address_, port_);
        conn_.reset();
        return false;
    }
    
    spdlog::info("Connected to {}:{}", address_, port_);
    return true;
}

void RpcClient::disconnect() {
    if (conn_) {
        conn_->close();
        conn_.reset();
    }
}

bool RpcClient::is_connected() const {
    return conn_ && conn_->socket_fd() >= 0;
}

bool RpcClient::receive_message(std::vector<uint8_t>& message) {
    if (!conn_) {
        return false;
    }
    
    uint32_t msg_len_net;
    if (!conn_->receive_exact(&msg_len_net, sizeof(msg_len_net))) {
        return false;
    }
    
    uint32_t msg_len = ntohl(msg_len_net);
    
    // Sanity check
    if (msg_len == 0 || msg_len > 100 * 1024 * 1024) { // 100MB max
        spdlog::error("Invalid message length: {}", msg_len);
        return false;
    }
    
    message.resize(msg_len);
    if (!conn_->receive_exact(message.data(), msg_len)) {
        return false;
    }
    
    return true;
}

bool RpcClient::send_message(const std::vector<uint8_t>& message) {
    if (!conn_) {
        return false;
    }
    
    uint32_t msg_len = message.size();
    uint32_t msg_len_net = htonl(msg_len);
    
    if (!conn_->send(&msg_len_net, sizeof(msg_len_net))) {
        return false;
    }
    
    if (!conn_->send(message)) {
        return false;
    }
    
    return true;
}

diarkis::commands::Response RpcClient::send_command(const diarkis::commands::Command& cmd) {
    diarkis::commands::Response resp;
    resp.success = false;
    
    if (!is_connected()) {
        if (!connect()) {
            resp.error = "Not connected to server";
            return resp;
        }
    }
    
    try {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, cmd);
        std::vector<uint8_t> request_data(sbuf.data(), sbuf.data() + sbuf.size());
        
        // Send request
        if (!send_message(request_data)) {
            resp.error = "Failed to send request";
            disconnect();
            return resp;
        }
        
        std::vector<uint8_t> response_data;
        if (!receive_message(response_data)) {
            resp.error = "Failed to receive response";
            disconnect();
            return resp;
        }
        
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(response_data.data()),
            response_data.size()
        );
        msgpack::object obj = oh.get();
        obj.convert(resp);
        
        return resp;
        
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("RPC error: ") + e.what();
        disconnect();
        return resp;
    }
}

}
