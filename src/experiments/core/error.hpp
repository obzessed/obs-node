#pragma once

/**
 * error.hpp - Error Types and Result Template
 */

#include <string>
#include <variant>
#include <optional>

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
    
    // Monadic operations
    
    // Map: Transform value if Ok, pass through error if Error
    template<typename Fn>
    auto Map(Fn&& fn) const -> Result<decltype(fn(std::declval<T>()))> {
        using U = decltype(fn(std::declval<T>()));
        if (IsOk()) {
            return Result<U>(fn(Value()));
        }
        return Result<U>(Error());
    }
    
    // FlatMap: Chain Result-returning functions
    template<typename Fn>
    auto FlatMap(Fn&& fn) const -> decltype(fn(std::declval<T>())) {
        using ResultU = decltype(fn(std::declval<T>()));
        if (IsOk()) {
            return fn(Value());
        }
        return ResultU(Error());
    }
    
    // MapError: Transform error if Error, pass through value if Ok
    template<typename Fn>
    Result<T> MapError(Fn&& fn) const {
        if (IsError()) {
            return Result<T>(fn(Error()));
        }
        return *this;
    }
    
private:
    std::variant<T, ScriptError> data_;
};

//=============================================================================
// Result<void> Specialization
//=============================================================================

template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(ScriptError error) : error_(std::move(error)) {}
    
    static Result Ok() { return Result(); }
    static Result Err(ScriptError error) { return Result(std::move(error)); }
    
    bool IsOk() const { return !error_.has_value(); }
    bool IsError() const { return error_.has_value(); }
    
    ScriptError& Error() { return error_.value(); }
    const ScriptError& Error() const { return error_.value(); }
    
    // MapError: Transform error if Error
    template<typename Fn>
    Result<void> MapError(Fn&& fn) const {
        if (IsError()) {
            return Result<void>(fn(Error()));
        }
        return *this;
    }
    
private:
    std::optional<ScriptError> error_;
};

} // namespace experiments
