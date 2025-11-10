
#ifndef DIARKIS_RPC_H
#define DIARKIS_RPC_H

#include <memory>
#include <vector>
#include <cstdint>
#include "diarkis/tcp.h"
#include "diarkis/state_machine.h"
#include "diarkis/commands.h"

namespace diarkis {

// Protocol: [4 bytes length (network order)][msgpack data]
class MessageProtocol {
public:
    static constexpr size_t MAX_MESSAGE_SIZE = 100 * 1024 * 1024; // 100MB
    
    static bool receive_message(std::shared_ptr<TcpConnection> conn, std::vector<uint8_t>& message);
    static bool send_message(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& message);
    
private:
    static bool receive_length(std::shared_ptr<TcpConnection> conn, uint32_t& length);
    static bool receive_data(std::shared_ptr<TcpConnection> conn, std::vector<uint8_t>& data, size_t length);
};

class RpcServer {
public:
    RpcServer(const std::string& address, uint16_t port, 
              std::shared_ptr<StateMachine> state_machine);
    ~RpcServer();
    
    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;
    
    bool start();
    void stop();
    bool is_running() const;
    size_t active_connections() const;

private:
    void handle_connection(std::shared_ptr<TcpConnection> conn);
    bool process_request(std::shared_ptr<TcpConnection> conn);
    
    commands::Response dispatch_command(const commands::Command& cmd);
    commands::Response handle_write_command(const commands::Command& cmd);
    commands::Response handle_read_command(const commands::Command& cmd);
    
    bool send_response(std::shared_ptr<TcpConnection> conn, 
                      const commands::Response& resp);
    void send_error_response(std::shared_ptr<TcpConnection> conn, 
                            const std::string& error);

    std::unique_ptr<TcpServer> tcp_server_;
    std::shared_ptr<StateMachine> state_machine_;
};

}

#endif
