
#include "diarkis/fs_operations.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <sstream>

namespace diarkis {

std::string FSOperation::serialize() const {
    std::string result;
    
    result.reserve(1 + 4 + path.size() + 4 + data.size());
    
    result.push_back(static_cast<char>(type)); // type
    
    // path length
    uint32_t path_len = static_cast<uint32_t>(path.size());
    result.push_back((path_len >> 0) & 0xFF);
    result.push_back((path_len >> 8) & 0xFF);
    result.push_back((path_len >> 16) & 0xFF);
    result.push_back((path_len >> 24) & 0xFF);
    
    result.append(path); // path
    
    // data length
    uint32_t data_len = static_cast<uint32_t>(data.size());
    result.push_back((data_len >> 0) & 0xFF);
    result.push_back((data_len >> 8) & 0xFF);
    result.push_back((data_len >> 16) & 0xFF);
    result.push_back((data_len >> 24) & 0xFF);
    
    // data
    if (data_len > 0) {
        result.append(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    return result;
}

std::optional<FSOperation> FSOperation::deserialize(const std::string& bytes) {
    if (bytes.size() < 9) {  // minimum 1 + 4 + 0 + 4 + 0
        spdlog::error("Deserialize failed: buffer too small (size={})", bytes.size());
        return std::nullopt;
    }
    
    size_t pos = 0;
    FSOperation op;
    
    // type
    op.type = static_cast<FSOperationType>(static_cast<uint8_t>(bytes[pos++]));
    
    // path length
    uint32_t path_len = 0;
    path_len |= (static_cast<uint8_t>(bytes[pos++]) << 0);
    path_len |= (static_cast<uint8_t>(bytes[pos++]) << 8);
    path_len |= (static_cast<uint8_t>(bytes[pos++]) << 16);
    path_len |= (static_cast<uint8_t>(bytes[pos++]) << 24);
    
    // path
    if (pos + path_len > bytes.size()) {
        spdlog::error("Deserialize failed: path_len={} exceeds buffer", path_len);
        return std::nullopt;
    }
    op.path = bytes.substr(pos, path_len);
    pos += path_len;
    
    // data length
    if (pos + 4 > bytes.size()) {
        spdlog::error("Deserialize failed: cannot read data length");
        return std::nullopt;
    }
    uint32_t data_len = 0;
    data_len |= (static_cast<uint8_t>(bytes[pos++]) << 0);
    data_len |= (static_cast<uint8_t>(bytes[pos++]) << 8);
    data_len |= (static_cast<uint8_t>(bytes[pos++]) << 16);
    data_len |= (static_cast<uint8_t>(bytes[pos++]) << 24);
    
    // data
    if (pos + data_len != bytes.size()) {
        spdlog::error("Deserialize failed: data_len={} doesn't match remaining bytes={}", 
                     data_len, bytes.size() - pos);
        return std::nullopt;
    }
    if (data_len > 0) {
        op.data.resize(data_len);
        std::memcpy(op.data.data(), bytes.data() + pos, data_len);
    }
    
    return op;
}

std::string FSOperation::to_string() const {
    std::ostringstream oss;
    oss << "FSOperation{type=" << op_type_to_string(type)
        << ", path=\"" << path << "\""
        << ", data_size=" << data.size() << "}";
    return oss.str();
}

const char* FSOperation::op_type_to_string(FSOperationType type) {
    switch (type) {
        case FSOperationType::CREATE_FILE: return "CREATE_FILE";
        case FSOperationType::WRITE_FILE: return "WRITE_FILE";
        case FSOperationType::APPEND_FILE: return "APPEND_FILE";
        case FSOperationType::DELETE_FILE: return "DELETE_FILE";
        case FSOperationType::CREATE_DIR: return "CREATE_DIR";
        case FSOperationType::DELETE_DIR: return "DELETE_DIR";
        case FSOperationType::RENAME: return "RENAME";
        default: return "UNKNOWN";
    }
}

} // namespace diarkis
