
#ifndef DIARKIS_TCP_H
#define DIARKIS_TCP_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdint>

namespace diarkis {

class TcpConnection {
public:
    explicit TcpConnection(int socket_fd);
    ~TcpConnection();
    
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    
    bool send(const void* data, size_t size);
    bool send(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> receive(size_t max_size = 65536);
    bool receive_exact(void* buffer, size_t size);
    
    const std::string& remote_address() const { return remote_addr_; }
    uint16_t remote_port() const { return remote_port_; }
    bool is_connected() const { return connected_.load(std::memory_order_acquire); }
    
    void close();

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
    struct Options {
        std::string address = "0.0.0.0";
        uint16_t port = 0;
        int listen_backlog = 128;
        int socket_timeout_sec = 30;
    };

    explicit TcpServer(const Options& opts);
    ~TcpServer();
    
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    
    bool start();
    void stop();
    bool is_running() const { return running_.load(std::memory_order_acquire); }
    
    void set_connection_handler(ConnectionHandler handler) {
        connection_handler_ = std::move(handler);
    }
    
    const std::string& address() const { return options_.address; }
    uint16_t port() const { return options_.port; }
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
    
    Options options_;
    int server_fd_;
    
    std::atomic<bool> running_;
    std::atomic<bool> should_stop_;
    
    std::unique_ptr<std::thread> accept_thread_;
    std::vector<std::thread> connection_threads_;
    
    mutable std::mutex connections_mutex_;
    std::vector<std::shared_ptr<TcpConnection>> active_connections_;
    
    ConnectionHandler connection_handler_;
};

}

#endif
