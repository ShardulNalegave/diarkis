
#include "diarkis/error.h"
#include <cerrno>
#include <cstring>

namespace diarkis {

std::string Error::to_string() const {
    if (message_.empty()) {
        switch (code_) {
            case ErrorCode::Success: return "Success";
            case ErrorCode::NotLeader: return "Not leader";
            case ErrorCode::NoLeaderAvailable: return "No leader available";
            case ErrorCode::FileNotFound: return "File not found";
            case ErrorCode::DirectoryNotFound: return "Directory not found";
            case ErrorCode::InvalidPath: return "Invalid path";
            case ErrorCode::AlreadyExists: return "Already exists";
            case ErrorCode::NotDirectory: return "Not a directory";
            case ErrorCode::IoError: return "I/O error";
            case ErrorCode::SerializationError: return "Serialization error";
            case ErrorCode::InvalidCommand: return "Invalid command";
            case ErrorCode::NetworkError: return "Network error";
            case ErrorCode::Timeout: return "Timeout";
            default: return "Unknown error";
        }
    }
    return message_;
}

Error Error::from_errno(int err) {
    switch (err) {
        case 0: return Error();
        case ENOENT: return Error(ErrorCode::FileNotFound, std::strerror(err));
        case EEXIST: return Error(ErrorCode::AlreadyExists, std::strerror(err));
        case ENOTDIR: return Error(ErrorCode::NotDirectory, std::strerror(err));
        default: return Error(ErrorCode::IoError, std::strerror(err));
    }
}

}
