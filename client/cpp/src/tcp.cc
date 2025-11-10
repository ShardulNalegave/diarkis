
#include "diarkis_client/tcp.h"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>

namespace diarkis_client {

TcpConnection::TcpConnection(const std::string& address, uint16_t port)
    : address_(address),
      port_(port),
      socket_fd_(-1),
      remote_port_(0) {
    
    std::memset(&socket_addr_, 0, sizeof(socket_addr_));
    
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        spdlog::error("Failed to create socket: {}", strerror(errno));
        return;
    }
    
    int flag = 1;
    if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        spdlog::warn("Failed to set TCP_NODELAY: {}", strerror(errno));
    }
    
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    socket_addr_.sin_family = AF_INET;
    socket_addr_.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, address_.c_str(), &socket_addr_.sin_addr) <= 0) {
        spdlog::error("Invalid address: {}", address_);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return;
    }
    
    if (::connect(socket_fd_, (sockaddr*)&socket_addr_, sizeof(socket_addr_)) < 0) {
        spdlog::error("Failed to connect to {}:{}: {}", address_, port_, strerror(errno));
        ::close(socket_fd_);
        socket_fd_ = -1;
        return;
    }
    
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(socket_fd_, (sockaddr*)&addr, &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        remote_addr_ = ip_str;
        remote_port_ = ntohs(addr.sin_port);
    }
    
    spdlog::debug("Connected to {}:{}", address_, port_);
}

TcpConnection::~TcpConnection() {
    close();
}

bool TcpConnection::send(const void* data, size_t size) {
    if (socket_fd_ < 0) {
        return false;
    }
    
    size_t total_sent = 0;
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    
    while (total_sent < size) {
        ssize_t sent = ::send(socket_fd_, ptr + total_sent, size - total_sent, MSG_NOSIGNAL);
        
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("Send failed: {}", strerror(errno));
            return false;
        }
        
        if (sent == 0) {
            spdlog::warn("Connection closed by peer during send");
            return false;
        }
        
        total_sent += sent;
    }
    
    return true;
}

bool TcpConnection::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

std::vector<uint8_t> TcpConnection::receive(size_t max_size) {
    if (socket_fd_ < 0) {
        return {};
    }
    
    std::vector<uint8_t> buffer(max_size);
    
    ssize_t received = ::recv(socket_fd_, buffer.data(), max_size, 0);
    
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return {};
        }
        spdlog::error("Receive failed: {}", strerror(errno));
        return {};
    }
    
    if (received == 0) {
        spdlog::info("Connection closed by peer");
        return {};
    }
    
    buffer.resize(received);
    return buffer;
}

bool TcpConnection::receive_exact(void* buffer, size_t size) {
    if (socket_fd_ < 0) {
        return false;
    }
    
    size_t total_received = 0;
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    
    while (total_received < size) {
        ssize_t received = ::recv(socket_fd_, ptr + total_received, size - total_received, 0);
        
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("Receive failed: {}", strerror(errno));
            return false;
        }
        
        if (received == 0) {
            spdlog::warn("Connection closed during receive");
            return false;
        }
        
        total_received += received;
    }
    
    return true;
}

void TcpConnection::close() {
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

}
