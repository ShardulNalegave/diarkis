
#ifndef DIARKIS_FS_OPERATIONS_H
#define DIARKIS_FS_OPERATIONS_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace diarkis {

enum class FSOperationType : uint8_t {
    CREATE_FILE = 1,
    WRITE_FILE = 2,
    APPEND_FILE = 3,
    DELETE_FILE = 4,
    CREATE_DIR = 5,
    DELETE_DIR = 6,
    RENAME = 7
};

/**
 * Represents a filesystem operation that can be serialized and replicated
 * 
 * Serialization format (binary):
 * [FSOperationType:1 byte][PathLen:4 bytes][Path:N bytes][DataLen:4 bytes][Data:M bytes]
 * 
 * For operations without data (e.g., DELETE), DataLen = 0
 * For RENAME, Data field contains the new path
 */
class FSOperation {
public:
    FSOperationType type;
    std::string path;
    std::vector<uint8_t> data;

    FSOperation() = default;
    
    FSOperation(FSOperationType op_type, std::string op_path)
        : type(op_type), path(std::move(op_path)) {}
    
    FSOperation(FSOperationType op_type, std::string op_path, std::vector<uint8_t> op_data)
        : type(op_type), path(std::move(op_path)), data(std::move(op_data)) {}

    /**
     * Serialize operation to byte string for Raft log
     * @return Serialized byte string
     */
    std::string serialize() const;

    /**
     * Deserialize operation from Raft log entry
     * @param bytes Serialized byte string
     * @return Deserialized operation, or nullopt on error
     */
    static std::optional<FSOperation> deserialize(const std::string& bytes);

    std::string to_string() const;

private:
    static const char* op_type_to_string(FSOperationType type);
};

} // namespace diarkis

#endif /* DIARKIS_FS_OPERATIONS_H */
