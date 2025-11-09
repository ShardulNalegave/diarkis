
#include "diarkis/rpc.h"
#include "spdlog/spdlog.h"
#include "arpa/inet.h"
#include "msgpack.hpp"
#include "diarkis/commands.h"

namespace diarkis {

RpcServer::RpcServer(const std::string& address, uint16_t port, std::shared_ptr<StateMachine> sm)
    : tcp_server_(std::make_unique<TcpServer>(address, port)),
      state_machine_(sm) {
    tcp_server_->set_connection_handler(
        [this](std::shared_ptr<TcpConnection> conn) {
            this->handle_connection(conn);
        }
    );
}

RpcServer::~RpcServer() {
    stop();
}

bool RpcServer::start() {
    spdlog::info("Starting RpcServer");
    return tcp_server_->start();
}

void RpcServer::stop() {
    spdlog::info("Stopping RpcServer");
    tcp_server_->stop();
}

bool RpcServer::is_running() const {
    return tcp_server_->is_running();
}

size_t RpcServer::active_connections() const {
    return tcp_server_->active_connections();
}

bool RpcServer::receive_message(std::shared_ptr<TcpConnection> conn, std::vector<uint8_t>& message) {
    uint32_t msg_len_net;
    if (!conn->receive_exact(&msg_len_net, sizeof(msg_len_net))) {
        return false;
    }
    
    uint32_t msg_len = ntohl(msg_len_net);
    
    // Sanity check
    if (msg_len == 0 || msg_len > 100 * 1024 * 1024) { // 100MB max
        spdlog::error("Invalid message length: {}", msg_len);
        return false;
    }
    
    // Read message data
    message.resize(msg_len);
    if (!conn->receive_exact(message.data(), msg_len)) {
        return false;
    }
    
    return true;
}

bool RpcServer::send_message(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& message) {
    uint32_t msg_len = message.size();
    uint32_t msg_len_net = htonl(msg_len);
    
    if (!conn->send(&msg_len_net, sizeof(msg_len_net))) {
        return false;
    }
    
    if (!conn->send(message)) {
        return false;
    }
    
    return true;
}

void RpcServer::handle_connection(std::shared_ptr<TcpConnection> conn) {
    spdlog::info("New RPC connection from {}:{}", conn->remote_address(), conn->remote_port());
    
    while (conn->is_connected()) {
        std::vector<uint8_t> request_data;
        
        if (!receive_message(conn, request_data)) {
            if (conn->is_connected()) {
                spdlog::error("Failed to receive message from {}:{}", 
                    conn->remote_address(), conn->remote_port());
            }
            break;
        }
        
        try {
            msgpack::object_handle oh = msgpack::unpack(
                reinterpret_cast<const char*>(request_data.data()), 
                request_data.size()
            );
            msgpack::object obj = oh.get();
            
            commands::Command cmd;
            obj.convert(cmd);
            
            spdlog::debug("Received command type={} path={}", 
                static_cast<int>(cmd.type), cmd.path);
            
            switch (cmd.type) {
                case commands::Type::WRITE_FILE:
                case commands::Type::APPEND_FILE:
                case commands::Type::CREATE_FILE:
                case commands::Type::CREATE_DIR:
                case commands::Type::DELETE_FILE:
                case commands::Type::DELETE_DIR:
                case commands::Type::RENAME:
                    process_write_request(conn, request_data);
                    break;
                    
                case commands::Type::READ_FILE:
                    process_read_request(conn, request_data);
                    break;
                    
                case commands::Type::LIST_DIR:
                    process_list_request(conn, request_data);
                    break;
                    
                default:
                    spdlog::error("Unknown command type: {}", static_cast<int>(cmd.type));
                    commands::Response resp;
                    resp.success = false;
                    resp.error = "Unknown command type";
                    
                    msgpack::sbuffer sbuf;
                    msgpack::pack(sbuf, resp);
                    std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
                    send_message(conn, response_data);
                    break;
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Error processing command: {}", e.what());
            
            commands::Response resp;
            resp.success = false;
            resp.error = std::string("Processing error: ") + e.what();
            
            msgpack::sbuffer sbuf;
            msgpack::pack(sbuf, resp);
            std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
            send_message(conn, response_data);
        }
    }
    
    spdlog::info("RPC connection closed: {}:{}", conn->remote_address(), conn->remote_port());
}

void RpcServer::process_write_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data) {
    try {
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(data.data()), 
            data.size()
        );
        msgpack::object obj = oh.get();
        
        commands::Command cmd;
        obj.convert(cmd);
        
        commands::Response resp = state_machine_->apply_write_command(cmd);
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
        
    } catch (const std::exception& e) {
        commands::Response resp;
        resp.success = false;
        resp.error = std::string("Write error: ") + e.what();
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
    }
}

void RpcServer::process_read_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data) {
    try {
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(data.data()), 
            data.size()
        );
        msgpack::object obj = oh.get();
        
        commands::Command cmd;
        obj.convert(cmd);
        
        commands::Response resp = state_machine_->apply_read_command(cmd);
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
        
    } catch (const std::exception& e) {
        commands::Response resp;
        resp.success = false;
        resp.error = std::string("Read error: ") + e.what();
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
    }
}

void RpcServer::process_list_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data) {
    try {
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(data.data()), 
            data.size()
        );
        msgpack::object obj = oh.get();
        
        commands::Command cmd;
        obj.convert(cmd);
        
        commands::Response resp = state_machine_->apply_read_command(cmd);
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
        
    } catch (const std::exception& e) {
        commands::Response resp;
        resp.success = false;
        resp.error = std::string("List error: ") + e.what();
        
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        send_message(conn, response_data);
    }
}

}
