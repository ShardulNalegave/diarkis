
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

static uint32_t readUint32(const uint8_t*& ptr) {
    uint32_t net_value;
    std::memcpy(&net_value, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    return ntohl(net_value);
}

static void writeUint8(std::vector<uint8_t>& buffer, uint8_t value) {
    buffer.push_back(value);
}

static uint8_t readUint8(const uint8_t*& ptr) {
    return *ptr++;
}

static void writeString(std::vector<uint8_t>& buffer, const std::string& str) {
    writeUint32(buffer, str.size());
    buffer.insert(buffer.end(), str.begin(), str.end());
}

static std::string readString(const uint8_t*& ptr) {
    uint32_t length = readUint32(ptr);
    
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

const char* Event::serialize() const {
    static thread_local std::vector<uint8_t> buffer;
    buffer.clear();
    
    writeUint8(buffer, VERSION);
    writeUint8(buffer, static_cast<uint8_t>(type));
    writeUint8(buffer, is_dir ? 1 : 0);
    
    // strings with length prefixes
    writeString(buffer, path);
    writeString(buffer, relative_path);
    writeString(buffer, old_path);
    writeString(buffer, contents);
    
    // this buffer is only valid until next call to serialize()
    return reinterpret_cast<const char*>(buffer.data());
}

Event Event::deserialize(const char* bytes) {
    Event event;

    if (!bytes) {
        spdlog::error("Cannot deserialize null pointer");
        event.type = EventType::INVALID;
        return event;
    }
    
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bytes);
    
    uint8_t version = readUint8(ptr);
    if (version != VERSION) {
        spdlog::error("Unsupported event version");
        event.type = EventType::INVALID;
        return event;
    }
    
    uint8_t type_byte = readUint8(ptr);
    if (type_byte > 4) {
        spdlog::error("Invalid event type");
        event.type = EventType::INVALID;
        return event;
    }
    event.type = static_cast<EventType>(type_byte);
    
    // Read is_dir flag
    uint8_t is_dir_byte = readUint8(ptr);
    event.is_dir = (is_dir_byte != 0);
    
    // Read strings
    event.path = readString(ptr);
    event.relative_path = readString(ptr);
    event.old_path = readString(ptr);
    event.contents = readString(ptr);
    
    return event;
}

};
