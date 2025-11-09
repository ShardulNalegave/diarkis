
#ifndef DIARKIS_RPC_H
#define DIARKIS_RPC_H

#include <memory>
#include "diarkis/tcp.h"
#include "diarkis/state_machine.h"

namespace diarkis {

class RpcServer {
public:
    RpcServer(const std::string& address, uint16_t port, std::shared_ptr<StateMachine> sm);
    ~RpcServer();
    
    bool start();
    void stop();
    bool is_running() const;
    
    size_t active_connections() const;
    
private:
    void handle_connection(std::shared_ptr<TcpConnection> conn);

    void process_write_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data);
    void process_read_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data);
    void process_list_request(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& data);
    
    bool receive_message(std::shared_ptr<TcpConnection> conn, std::vector<uint8_t>& message);
    bool send_message(std::shared_ptr<TcpConnection> conn, const std::vector<uint8_t>& message);

    std::unique_ptr<TcpServer> tcp_server_;
    std::shared_ptr<StateMachine> state_machine_;
};

}

#endif
