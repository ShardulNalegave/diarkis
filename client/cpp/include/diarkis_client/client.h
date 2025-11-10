
#ifndef DIARKIS_CLIENT_H
#define DIARKIS_CLIENT_H

#include "stdint.h"
#include <string>
#include <vector>
#include "diarkis_client/rpc.h"

namespace diarkis_client {

class Client {
public:
    explicit Client(const std::string& address, uint16_t port);

    int create_file(std::string& path);
    int create_directory(std::string& path);

    size_t read_file(std::string& path, uint8_t* buffer);
    int write_file(std::string& path, uint8_t* buffer, size_t size);
    int append_file(std::string& path, uint8_t* buffer, size_t size);

    int rename_file(std::string& old_path, std::string& new_path);

    int delete_file(std::string& path);
    int delete_directory(std::string& path);

    std::vector<std::string> list_directory(std::string& path);

private:
    RpcClient rpc_;
};

}

#endif
