
#ifndef DIARKIS_CLIENT_TCP_H
#define DIARKIS_CLIENT_TCP_H

#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>

namespace diarkis_client {

class TcpConnection {
public:
    explicit TcpConnection(const std::string& address, uint16_t port);
    ~TcpConnection();
    
    // disable copy
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    
    bool send(const void* data, size_t size);
    bool send(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> receive(size_t max_size = 65536);
    bool receive_exact(void* buffer, size_t size);

    void close();
    
    std::string address() const { return address_; }
    uint16_t port() const { return port_; }

    std::string remote_address() const { return remote_addr_; };
    uint16_t remote_port() const { return remote_port_; };
    
    int socket_fd() const { return socket_fd_; }
    
private:
    std::string address_;
    uint16_t port_;
    
    int socket_fd_;
    sockaddr_in socket_addr_;
    
    std::string remote_addr_;
    uint16_t remote_port_;
};

}

#endif
