
#ifndef DIARKIS_TCP_H
#define DIARKIS_TCP_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>

namespace diarkis {

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    explicit TcpConnection(int socket_fd);
    ~TcpConnection();
    
    // disable copy
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    
    bool send(const void* data, size_t size);
    bool send(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> receive(size_t max_size = 65536);
    bool receive_exact(void* buffer, size_t size);
    
    std::string remote_address() const;
    uint16_t remote_port() const;
    bool is_connected() const;
    
    void close();
    
    int socket_fd() const { return socket_fd_; }
    
private:
    int socket_fd_;
    std::atomic<bool> connected_;
    mutable std::mutex socket_mutex_;
    
    std::string remote_addr_;
    uint16_t remote_port_;
};

using ConnectionHandler = std::function<void(std::shared_ptr<TcpConnection>)>;

class TcpServer {
public:
    explicit TcpServer(const std::string& address, uint16_t port);
    ~TcpServer();
    
    // disable copy
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    void set_connection_handler(ConnectionHandler handler) {
        connection_handler_ = std::move(handler);
    }
    
    std::string address() const { return address_; }
    uint16_t port() const { return port_; }
    size_t active_connections() const;
    
private:
    void accept_loop();
    
    void handle_connection(std::shared_ptr<TcpConnection> conn);
    
    void add_connection(std::shared_ptr<TcpConnection> conn);
    void remove_connection(std::shared_ptr<TcpConnection> conn);
    void cleanup_connections();
    
    bool create_socket();
    bool bind_socket();
    bool listen_socket();
    void close_socket();
    
    std::string address_;
    uint16_t port_;
    
    int server_fd_;
    sockaddr_in server_addr_;
    
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    std::unique_ptr<std::thread> accept_thread_;
    std::vector<std::thread> connection_threads_;
    mutable std::mutex connections_mutex_;
    std::vector<std::shared_ptr<TcpConnection>> active_connections_;
    
    ConnectionHandler connection_handler_;
    
    static constexpr int LISTEN_BACKLOG = 128;
    static constexpr int SOCKET_TIMEOUT_SEC = 30;
};

}

#endif
