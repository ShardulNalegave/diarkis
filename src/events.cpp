#include "diarkis/events.h"

#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>
#include <stdint.h>
#include <spdlog/spdlog.h>

namespace events {

static void writeUint32(std::vector<uint8_t>& buffer, uint32_t value) {
    uint32_t net_value = htonl(value);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&net_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(uint32_t));
}

static uint32_t readUint32(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr + sizeof(uint32_t) > end) {
        throw std::runtime_error("Buffer overflow while reading uint32");
    }
    uint32_t net_value;
    std::memcpy(&net_value, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    return ntohl(net_value);
}

static void writeUint8(std::vector<uint8_t>& buffer, uint8_t value) {
    buffer.push_back(value);
}

static uint8_t readUint8(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr >= end) {
        throw std::runtime_error("Buffer overflow while reading uint8");
    }
    return *ptr++;
}

static void writeString(std::vector<uint8_t>& buffer, const std::string& str) {
    writeUint32(buffer, str.size());
    buffer.insert(buffer.end(), str.begin(), str.end());
}

static std::string readString(const uint8_t*& ptr, const uint8_t* end) {
    uint32_t length = readUint32(ptr, end);
    
    const uint32_t MAX_STRING_SIZE = 100 * 1024 * 1024; // 100MB max
    if (length > MAX_STRING_SIZE) {
        throw std::runtime_error("String length exceeds maximum allowed size");
    }
    
    if (ptr + length > end) {
        throw std::runtime_error("Buffer overflow while reading string data");
    }
    
    std::string result(reinterpret_cast<const char*>(ptr), length);
    ptr += length;
    
    return result;
}

static const uint8_t VERSION = 1;

size_t Event::getSerializedSize() const {
    size_t size = 0;
    size += sizeof(uint8_t);   // version
    size += sizeof(uint8_t);   // type
    size += sizeof(uint8_t);   // is_dir
    
    // length prefix + data
    size += sizeof(uint32_t) + path.size();
    size += sizeof(uint32_t) + relative_path.size();
    size += sizeof(uint32_t) + old_path.size();
    size += sizeof(uint32_t) + contents.size();
    
    return size;
}

std::pair<const char*, size_t> Event::serialize() const {
    static thread_local std::vector<uint8_t> buffer;
    buffer.clear();
    buffer.reserve(getSerializedSize());
    
    writeUint8(buffer, VERSION);
    writeUint8(buffer, static_cast<uint8_t>(type));
    writeUint8(buffer, is_dir ? 1 : 0);
    
    // strings with length prefixes
    writeString(buffer, path);
    writeString(buffer, relative_path);
    writeString(buffer, old_path);
    writeString(buffer, contents);
    
    // this buffer is only valid until next call to serialize()
    return {reinterpret_cast<const char*>(buffer.data()), buffer.size()};
}

Event Event::deserialize(const char* bytes, size_t size) {
    Event event;

    if (!bytes || size == 0) {
        spdlog::error("Cannot deserialize null or empty buffer");
        event.type = EventType::INVALID;
        return event;
    }
    
    try {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bytes);
        const uint8_t* end = ptr + size;
        
        uint8_t version = readUint8(ptr, end);
        if (version != VERSION) {
            spdlog::error("Unsupported event version: {} (expected {})", version, VERSION);
            event.type = EventType::INVALID;
            return event;
        }
        
        uint8_t type_byte = readUint8(ptr, end);
        if (type_byte > 4) {
            spdlog::error("Invalid event type: {}", type_byte);
            event.type = EventType::INVALID;
            return event;
        }
        event.type = static_cast<EventType>(type_byte);

        uint8_t is_dir_byte = readUint8(ptr, end);
        event.is_dir = (is_dir_byte != 0);

        event.path = readString(ptr, end);
        event.relative_path = readString(ptr, end);
        event.old_path = readString(ptr, end);
        event.contents = readString(ptr, end);

        if (ptr != end) {
            spdlog::warn("Deserialization warning: {} bytes remaining in buffer", end - ptr);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Deserialization failed: {}", e.what());
        event.type = EventType::INVALID;
    }
    
    return event;
}

};