
#include "diarkis/rpc.h"
#include "spdlog/spdlog.h"
#include "msgpack.hpp"
#include <arpa/inet.h>

namespace diarkis {

bool MessageProtocol::receive_length(std::shared_ptr<TcpConnection> conn, uint32_t& length) {
    uint32_t length_net;
    if (!conn->receive_exact(&length_net, sizeof(length_net))) {
        return false;
    }
    
    length = ntohl(length_net);
    
    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        spdlog::error("Invalid message length: {}", length);
        return false;
    }
    
    return true;
}

bool MessageProtocol::receive_data(std::shared_ptr<TcpConnection> conn, 
                                   std::vector<uint8_t>& data, size_t length) {
    data.resize(length);
    return conn->receive_exact(data.data(), length);
}

bool MessageProtocol::receive_message(std::shared_ptr<TcpConnection> conn, 
                                     std::vector<uint8_t>& message) {
    uint32_t length;
    if (!receive_length(conn, length)) {
        return false;
    }
    
    return receive_data(conn, message, length);
}

bool MessageProtocol::send_message(std::shared_ptr<TcpConnection> conn, 
                                  const std::vector<uint8_t>& message) {
    if (message.size() > MAX_MESSAGE_SIZE) {
        spdlog::error("Message too large: {} bytes", message.size());
        return false;
    }
    
    uint32_t length = static_cast<uint32_t>(message.size());
    uint32_t length_net = htonl(length);
    
    if (!conn->send(&length_net, sizeof(length_net))) {
        return false;
    }
    
    return conn->send(message);
}

// RpcServer implementation
RpcServer::RpcServer(const std::string& address, uint16_t port, 
                     std::shared_ptr<StateMachine> state_machine)
    : state_machine_(std::move(state_machine)) {
    
    TcpServer::Options opts;
    opts.address = address;
    opts.port = port;
    
    tcp_server_ = std::make_unique<TcpServer>(opts);
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
    spdlog::info("Starting RPC server");
    return tcp_server_->start();
}

void RpcServer::stop() {
    spdlog::info("Stopping RPC server");
    if (tcp_server_) {
        tcp_server_->stop();
    }
}

bool RpcServer::is_running() const {
    return tcp_server_ && tcp_server_->is_running();
}

size_t RpcServer::active_connections() const {
    return tcp_server_ ? tcp_server_->active_connections() : 0;
}

void RpcServer::handle_connection(std::shared_ptr<TcpConnection> conn) {
    spdlog::info("New RPC connection from {}:{}", 
                 conn->remote_address(), conn->remote_port());
    
    while (conn->is_connected()) {
        if (!process_request(conn)) {
            if (conn->is_connected()) {
                spdlog::error("Failed to process request from {}:{}", 
                             conn->remote_address(), conn->remote_port());
            }
            break;
        }
    }
    
    spdlog::info("RPC connection closed: {}:{}", 
                 conn->remote_address(), conn->remote_port());
}

bool RpcServer::process_request(std::shared_ptr<TcpConnection> conn) {
    std::vector<uint8_t> request_data;
    
    if (!MessageProtocol::receive_message(conn, request_data)) {
        return false;
    }
    
    try {
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(request_data.data()), 
            request_data.size()
        );
        
        commands::Command cmd;
        oh.get().convert(cmd);
        
        spdlog::debug("Received command: type={}, path={}", 
                     static_cast<int>(cmd.type), cmd.path);
        
        commands::Response resp = dispatch_command(cmd);
        return send_response(conn, resp);
        
    } catch (const msgpack::unpack_error& e) {
        spdlog::error("MessagePack unpack error: {}", e.what());
        send_error_response(conn, "Deserialization error");
        return false;
    } catch (const msgpack::type_error& e) {
        spdlog::error("MessagePack type error: {}", e.what());
        send_error_response(conn, "Type conversion error");
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Error processing command: {}", e.what());
        send_error_response(conn, std::string("Processing error: ") + e.what());
        return false;
    }
}

commands::Response RpcServer::dispatch_command(const commands::Command& cmd) {
    switch (cmd.type) {
        case commands::Type::WRITE_FILE:
        case commands::Type::APPEND_FILE:
        case commands::Type::CREATE_FILE:
        case commands::Type::CREATE_DIR:
        case commands::Type::DELETE_FILE:
        case commands::Type::DELETE_DIR:
        case commands::Type::RENAME:
            return handle_write_command(cmd);
            
        case commands::Type::READ_FILE:
        case commands::Type::LIST_DIR:
            return handle_read_command(cmd);
            
        default:
            spdlog::error("Unknown command type: {}", static_cast<int>(cmd.type));
            commands::Response resp;
            resp.success = false;
            resp.error = "Unknown command type";
            return resp;
    }
}

commands::Response RpcServer::handle_write_command(const commands::Command& cmd) {
    return state_machine_->apply_write_command(cmd);
}

commands::Response RpcServer::handle_read_command(const commands::Command& cmd) {
    return state_machine_->apply_read_command(cmd);
}

bool RpcServer::send_response(std::shared_ptr<TcpConnection> conn, 
                              const commands::Response& resp) {
    try {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, resp);
        
        std::vector<uint8_t> response_data(sbuf.data(), sbuf.data() + sbuf.size());
        return MessageProtocol::send_message(conn, response_data);
        
    } catch (const std::exception& e) {
        spdlog::error("Error serializing response: {}", e.what());
        return false;
    }
}

void RpcServer::send_error_response(std::shared_ptr<TcpConnection> conn, 
                                    const std::string& error) {
    commands::Response resp;
    resp.success = false;
    resp.error = error;
    send_response(conn, resp);
}

}
