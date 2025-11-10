
#ifndef DIARKIS_RESULT_H
#define DIARKIS_RESULT_H

#include "diarkis/error.h"
#include <variant>
#include <type_traits>

namespace diarkis {

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}
    
    bool ok() const { 
        return std::holds_alternative<T>(data_); 
    }
    
    const T& value() const { 
        return std::get<T>(data_); 
    }
    
    T& value() { 
        return std::get<T>(data_); 
    }
    
    const Error& error() const { 
        return std::get<Error>(data_); 
    }
    
    T value_or(T default_value) const {
        return ok() ? value() : std::move(default_value);
    }
    
private:
    std::variant<T, Error> data_;
};

template<>
class Result<void> {
public:
    Result() : error_(ErrorCode::Success) {}
    Result(Error error) : error_(std::move(error)) {}
    
    bool ok() const { return error_.ok(); }
    const Error& error() const { return error_; }
    
private:
    Error error_;
};

}

#endif
