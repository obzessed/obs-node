#pragma once

/**
 * error.hpp - Error Types and Result Template
 */

#include <string>
#include <variant>

namespace experiments {

//=============================================================================
// Error Types
//=============================================================================

enum class ErrorCode {
    None = 0,
    NotInitialized,
    AlreadyInitialized,
    EnvironmentCreationFailed,
    CompileError,
    RuntimeError,
    Timeout,
    Cancelled,
    FileNotFound,
    FileReadError,
    InvalidArgument,
    InternalError
};

struct ScriptError {
    ErrorCode code{ErrorCode::None};
    std::string message;
    std::string stack;
    int line{0};
    int column{0};
    
    bool IsError() const { return code != ErrorCode::None; }
    operator bool() const { return IsError(); }
    
    static ScriptError None() { return {}; }
    
    static ScriptError Make(ErrorCode code, const std::string& msg) {
        return {code, msg, "", 0, 0};
    }
};

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(ScriptError error) : data_(std::move(error)) {}
    
    bool IsOk() const { return std::holds_alternative<T>(data_); }
    bool IsError() const { return std::holds_alternative<ScriptError>(data_); }
    
    T& Value() { return std::get<T>(data_); }
    const T& Value() const { return std::get<T>(data_); }
    ScriptError& Error() { return std::get<ScriptError>(data_); }
    const ScriptError& Error() const { return std::get<ScriptError>(data_); }
    
    T ValueOr(T default_value) const {
        return IsOk() ? Value() : default_value;
    }
    
private:
    std::variant<T, ScriptError> data_;
};

} // namespace experiments
