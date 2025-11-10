
#ifndef DIARKIS_ERROR_H
#define DIARKIS_ERROR_H

#include <system_error>
#include <string>

namespace diarkis {

enum class ErrorCode {
    Success = 0,
    NotLeader,
    NoLeaderAvailable,
    FileNotFound,
    DirectoryNotFound,
    InvalidPath,
    AlreadyExists,
    NotDirectory,
    IoError,
    SerializationError,
    InvalidCommand,
    NetworkError,
    Timeout,
    Unknown
};

class Error {
public:
    Error() : code_(ErrorCode::Success) {}
    explicit Error(ErrorCode code, std::string message = "")
        : code_(code), message_(std::move(message)) {}
    
    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    bool ok() const { return code_ == ErrorCode::Success; }
    
    std::string to_string() const;
    
    static Error from_errno(int err);
    
private:
    ErrorCode code_;
    std::string message_;
};

}

#endif
