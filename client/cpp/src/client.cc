
#include "diarkis_client/client.h"
#include "diarkis/commands.h"
#include "spdlog/spdlog.h"

namespace diarkis_client {

Client::Client(const std::string& address, uint16_t port)
    : rpc_(address, port) {
}

int Client::create_file(std::string& path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::CREATE_FILE;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("create_file failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

int Client::create_directory(std::string& path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::CREATE_DIR;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("create_directory failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

size_t Client::read_file(std::string& path, uint8_t* buffer) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::READ_FILE;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("read_file failed: {}", resp.error);
        return 0;
    }
    
    if (resp.data.empty()) {
        return 0;
    }
    
    std::memcpy(buffer, resp.data.data(), resp.data.size());
    return resp.data.size();
}

int Client::write_file(std::string& path, uint8_t* buffer, size_t size) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::WRITE_FILE;
    cmd.path = path;
    cmd.contents.assign(buffer, buffer + size);
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("write_file failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

int Client::append_file(std::string& path, uint8_t* buffer, size_t size) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::APPEND_FILE;
    cmd.path = path;
    cmd.contents.assign(buffer, buffer + size);
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("append_file failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

int Client::rename_file(std::string& old_path, std::string& new_path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::RENAME;
    cmd.path = old_path;
    cmd.new_path = new_path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("rename_file failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

int Client::delete_file(std::string& path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::DELETE_FILE;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("delete_file failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

int Client::delete_directory(std::string& path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::DELETE_DIR;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("delete_directory failed: {}", resp.error);
        return -1;
    }
    
    return 0;
}

std::vector<std::string> Client::list_directory(std::string& path) {
    diarkis::commands::Command cmd;
    cmd.type = diarkis::commands::Type::LIST_DIR;
    cmd.path = path;
    
    diarkis::commands::Response resp = rpc_.send_command(cmd);
    
    if (!resp.success) {
        spdlog::error("list_directory failed: {}", resp.error);
        return {};
    }
    
    return resp.entries;
}

}
