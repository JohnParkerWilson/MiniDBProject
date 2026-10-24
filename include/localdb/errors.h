#pragma once

#include <stdexcept>
#include <string>

namespace localdb {

enum class ErrorCode {
    InvalidArgument,
    InvalidSchema,
    TypeMismatch,
    SerializationError,
    Corruption,
    IoError,
    PageNotFound,
};

class DatabaseError : public std::runtime_error {
public:
    DatabaseError(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

}  // namespace localdb
