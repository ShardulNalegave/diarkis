
#include "diarkis/tcp.h"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <algorithm>

namespace diarkis {

TcpConnection::TcpConnection(int socket_fd) 
    : socket_fd_(socket_fd), 
      connected_(true), 
      remote_port_(0) {
    
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(socket_fd_, (sockaddr*)&addr, &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        remote_addr_ = ip_str;
        remote_port_ = ntohs(addr.sin_port);
    }
    
    spdlog::debug("TcpConnection created: {}:{}", remote_addr_, remote_port_);
}

TcpConnection::~TcpConnection() {
    close();
    spdlog::debug("TcpConnection destroyed: {}:{}", remote_addr_, remote_port_);
}

bool TcpConnection::send(const void* data, size_t size) {
    if (!connected_.load(std::memory_order_acquire) || socket_fd_ < 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    size_t total_sent = 0;
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    
    while (total_sent < size) {
        ssize_t sent = ::send(socket_fd_, ptr + total_sent, size - total_sent, MSG_NOSIGNAL);
        
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("Send failed on {}:{}: {}", remote_addr_, remote_port_, strerror(errno));
            connected_.store(false, std::memory_order_release);
            return false;
        }
        
        if (sent == 0) {
            spdlog::warn("Connection closed by peer during send: {}:{}", remote_addr_, remote_port_);
            connected_.store(false, std::memory_order_release);
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
    if (!connected_.load(std::memory_order_acquire) || socket_fd_ < 0) {
        return {};
    }
    
    std::vector<uint8_t> buffer(max_size);
    
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    ssize_t received = ::recv(socket_fd_, buffer.data(), max_size, 0);
    
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return {};
        }
        spdlog::error("Receive failed on {}:{}: {}", remote_addr_, remote_port_, strerror(errno));
        connected_.store(false, std::memory_order_release);
        return {};
    }
    
    if (received == 0) {
        spdlog::info("Connection closed by peer: {}:{}", remote_addr_, remote_port_);
        connected_.store(false, std::memory_order_release);
        return {};
    }
    
    buffer.resize(received);
    return buffer;
}

bool TcpConnection::receive_exact(void* buffer, size_t size) {
    if (!connected_.load(std::memory_order_acquire) || socket_fd_ < 0) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(socket_mutex_);
    
    size_t total_received = 0;
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    
    while (total_received < size) {
        ssize_t received = ::recv(socket_fd_, ptr + total_received, size - total_received, 0);
        
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("Receive failed on {}:{}: {}", remote_addr_, remote_port_, strerror(errno));
            connected_.store(false, std::memory_order_release);
            return false;
        }
        
        if (received == 0) {
            spdlog::warn("Connection closed during receive from {}:{}", remote_addr_, remote_port_);
            connected_.store(false, std::memory_order_release);
            return false;
        }
        
        total_received += received;
    }
    
    return true;
}

void TcpConnection::close() {
    if (connected_.exchange(false, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ >= 0) {
            ::shutdown(socket_fd_, SHUT_RDWR);
            ::close(socket_fd_);
            socket_fd_ = -1;
        }
    }
}

TcpServer::TcpServer(const Options& opts)
    : options_(opts),
      server_fd_(-1),
      running_(false),
      should_stop_(false) {
}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (running_.load(std::memory_order_acquire)) {
        spdlog::warn("TcpServer already running");
        return false;
    }
    
    spdlog::info("Starting TcpServer on {}:{}", options_.address, options_.port);
    
    if (!create_socket()) {
        return false;
    }
    
    if (!bind_socket()) {
        close_socket();
        return false;
    }
    
    if (!listen_socket()) {
        close_socket();
        return false;
    }
    
    running_.store(true, std::memory_order_release);
    should_stop_.store(false, std::memory_order_release);
    
    accept_thread_ = std::make_unique<std::thread>(&TcpServer::accept_loop, this);
    
    spdlog::info("TcpServer started successfully");
    return true;
}

void TcpServer::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    spdlog::info("Stopping TcpServer...");
    
    should_stop_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    
    close_socket();
    
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    accept_thread_.reset();
    
    cleanup_connections();
    
    for (auto& thread : connection_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    connection_threads_.clear();
    
    spdlog::info("TcpServer stopped");
}

size_t TcpServer::active_connections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return active_connections_.size();
}

void TcpServer::accept_loop() {
    spdlog::info("Accept loop started");
    
    while (!should_stop_.load(std::memory_order_acquire)) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = ::accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (should_stop_.load(std::memory_order_acquire)) {
                break;
            }
            
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            
            spdlog::error("Accept failed: {}", strerror(errno));
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        uint16_t client_port = ntohs(client_addr.sin_port);
        
        spdlog::info("New connection from {}:{}", client_ip, client_port);
        
        // Set TCP_NODELAY to disable Nagle's algorithm
        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
        struct timeval timeout;
        timeout.tv_sec = options_.socket_timeout_sec;
        timeout.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        auto conn = std::make_shared<TcpConnection>(client_fd);
        add_connection(conn);
        
        connection_threads_.emplace_back(&TcpServer::handle_connection, this, conn);
    }
    
    spdlog::info("Accept loop stopped");
}

void TcpServer::handle_connection(std::shared_ptr<TcpConnection> conn) {
    spdlog::debug("Handling connection: {}:{}", conn->remote_address(), conn->remote_port());
    
    try {
        if (connection_handler_) {
            connection_handler_(conn);
        } else {
            spdlog::warn("No connection handler set, closing connection");
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception in connection handler for {}:{}: {}", 
                     conn->remote_address(), conn->remote_port(), e.what());
    }
    
    conn->close();
    remove_connection(conn);
    
    spdlog::debug("Connection handler finished: {}:{}", 
                 conn->remote_address(), conn->remote_port());
}

void TcpServer::add_connection(std::shared_ptr<TcpConnection> conn) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    active_connections_.push_back(conn);
    spdlog::debug("Active connections: {}", active_connections_.size());
}

void TcpServer::remove_connection(std::shared_ptr<TcpConnection> conn) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = std::find(active_connections_.begin(), active_connections_.end(), conn);
    if (it != active_connections_.end()) {
        active_connections_.erase(it);
    }
    spdlog::debug("Active connections: {}", active_connections_.size());
}

void TcpServer::cleanup_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    spdlog::info("Closing {} active connections", active_connections_.size());
    
    for (auto& conn : active_connections_) {
        conn->close();
    }
    
    active_connections_.clear();
}

bool TcpServer::create_socket() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    
    if (server_fd_ < 0) {
        spdlog::error("Failed to create socket: {}", strerror(errno));
        return false;
    }
    
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        spdlog::warn("Failed to set SO_REUSEADDR: {}", strerror(errno));
    }
    
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        spdlog::warn("Failed to set SO_REUSEPORT: {}", strerror(errno));
    }
    
    return true;
}

bool TcpServer::bind_socket() {
    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(options_.port);
    
    if (options_.address == "0.0.0.0" || options_.address.empty()) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, options_.address.c_str(), &server_addr.sin_addr) <= 0) {
            spdlog::error("Invalid address: {}", options_.address);
            return false;
        }
    }
    
    if (::bind(server_fd_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        spdlog::error("Failed to bind to {}:{}: {}", options_.address, options_.port, strerror(errno));
        return false;
    }
    
    spdlog::info("Bound to {}:{}", options_.address, options_.port);
    return true;
}

bool TcpServer::listen_socket() {
    if (::listen(server_fd_, options_.listen_backlog) < 0) {
        spdlog::error("Failed to listen: {}", strerror(errno));
        return false;
    }
    
    spdlog::info("Listening with backlog: {}", options_.listen_backlog);
    return true;
}

void TcpServer::close_socket() {
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
}

}
