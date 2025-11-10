
#ifndef DIARKIS_CLIENT_RPC_H
#define DIARKIS_CLIENT_RPC_H

#include <memory>
#include <vector>
#include "diarkis_client/tcp.h"
#include "diarkis/commands.h"

namespace diarkis_client {

class RpcClient {
public:
    RpcClient(const std::string& address, uint16_t port);
    ~RpcClient();
    
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    diarkis::commands::Response send_command(const diarkis::commands::Command& cmd);
    
private:    
    bool receive_message(std::vector<uint8_t>& message);
    bool send_message(const std::vector<uint8_t>& message);

    std::string address_;
    uint16_t port_;
    std::unique_ptr<TcpConnection> conn_;
};

}

#endif
