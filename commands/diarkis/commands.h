
#ifndef DIARKIS_COMMANDS_H
#define DIARKIS_COMMANDS_H

#include <string>
#include <vector>
#include <stdint.h>
#include <msgpack.hpp>

namespace diarkis::commands {

enum class Type : uint8_t {
    CREATE_FILE = 1,
    READ_FILE = 2,
    WRITE_FILE = 3,
    APPEND_FILE = 4,
    DELETE_FILE = 5,
    CREATE_DIR = 6,
    LIST_DIR = 7,
    DELETE_DIR = 8,
    RENAME = 9
};

struct Command {
    Type type;
    std::string path;
    
    std::string new_path;              // For RENAME
    std::vector<uint8_t> contents;     // For WRITE/APPEND/READ response
    
    Command() {}

    // without data
    Command(Type _type, std::string _path) 
        : type(_type), path(std::move(_path)) {}
    
    // for WRITE/APPEND
    Command(Type _type, std::string _path, std::vector<uint8_t> _data)
        : type(_type), path(std::move(_path)), contents(std::move(_data)) {}
    
    // for RENAME
    Command(Type _type, std::string _path, std::string _new_path)
        : type(_type), path(std::move(_path)), new_path(std::move(_new_path)) {}
    
    MSGPACK_DEFINE(type, path, new_path, contents);
};

struct Response {
    bool success;
    std::string error;
    std::vector<uint8_t> data;              // For READ responses
    std::vector<std::string> entries;       // For LIST_DIR responses
    
    Response() : success(false) {}
    
    MSGPACK_DEFINE(success, error, data, entries);
};

}

MSGPACK_ADD_ENUM(diarkis::commands::Type);

#endif
