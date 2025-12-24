#pragma once

/**
 * script_result.hpp - Result type for script execution
 * 
 * Wraps either a ScriptValue (success) or ScriptError (failure).
 * Provides a simple API for external code.
 */

#include "script_value.hpp"
#include "../core/error.hpp"
#include <variant>

namespace experiments {

/**
 * ScriptResult - Either a value or an error
 */
class ScriptResult {
public:
    // Constructors
    ScriptResult() = default;
    ScriptResult(ScriptValue value) : data_(std::move(value)) {}
    ScriptResult(ScriptError error) : data_(std::move(error)) {}
    
    // Status checks
    bool IsOk() const { return std::holds_alternative<ScriptValue>(data_); }
    bool IsError() const { return std::holds_alternative<ScriptError>(data_); }
    explicit operator bool() const { return IsOk(); }
    
    // Access value (throws if error)
    ScriptValue& Value() {
        if (!IsOk()) throw std::runtime_error("ScriptResult contains error, not value");
        return std::get<ScriptValue>(data_);
    }
    const ScriptValue& Value() const {
        if (!IsOk()) throw std::runtime_error("ScriptResult contains error, not value");
        return std::get<ScriptValue>(data_);
    }
    
    // Access error (throws if ok)
    ScriptError& Error() {
        if (!IsError()) throw std::runtime_error("ScriptResult contains value, not error");
        return std::get<ScriptError>(data_);
    }
    const ScriptError& Error() const {
        if (!IsError()) throw std::runtime_error("ScriptResult contains value, not error");
        return std::get<ScriptError>(data_);
    }
    
    // Safe access (returns nullptr if wrong type)
    ScriptValue* TryValue() { return IsOk() ? &std::get<ScriptValue>(data_) : nullptr; }
    const ScriptValue* TryValue() const { return IsOk() ? &std::get<ScriptValue>(data_) : nullptr; }
    ScriptError* TryError() { return IsError() ? &std::get<ScriptError>(data_) : nullptr; }
    const ScriptError* TryError() const { return IsError() ? &std::get<ScriptError>(data_) : nullptr; }
    
    // Convenience: extract primitives directly
    std::string ToString() const { return IsOk() ? Value().ToString() : Error().message; }
    std::optional<double> ToNumber() const { return IsOk() ? Value().ToNumber() : std::nullopt; }
    std::optional<bool> ToBool() const { return IsOk() ? Value().ToBool() : std::nullopt; }
    
    // Static constructors
    static ScriptResult Ok(ScriptValue value) { return ScriptResult(std::move(value)); }
    static ScriptResult Err(ScriptError error) { return ScriptResult(std::move(error)); }
    static ScriptResult Err(ErrorCode code, const std::string& msg) {
        return ScriptResult(ScriptError::Make(code, msg));
    }
    
    //=========================================================================
    // Monadic Operations (Functional Composition)
    //=========================================================================
    
    // Transform: Apply function to value if Ok, propagate error otherwise
    // Usage: result.Transform([](ScriptValue& v) { return v.ToString(); })
    template<typename F>
    auto Transform(F&& fn) const -> ScriptResult {
        if (IsOk()) {
            return ScriptResult::Ok(fn(Value()));
        }
        return ScriptResult::Err(Error());
    }
    
    // AndThen: Chain operations that return ScriptResult (flatMap/bind)
    // Usage: result.AndThen([&](ScriptValue& v) { return env.ExecuteSync(v.ToString()); })
    template<typename F>
    auto AndThen(F&& fn) const -> ScriptResult {
        if (IsOk()) {
            return fn(Value());
        }
        return ScriptResult::Err(Error());
    }
    
    // OrElse: Provide alternative on error
    // Usage: result.OrElse([](const ScriptError& e) { return ScriptResult::Ok(defaultVal); })
    template<typename F>
    auto OrElse(F&& fn) const -> ScriptResult {
        if (IsOk()) {
            return *this;
        }
        return fn(Error());
    }
    
    // ValueOr: Get value or default if error
    // Usage: auto val = result.ValueOr(defaultValue);
    ScriptValue ValueOr(ScriptValue default_value) const {
        return IsOk() ? Value() : std::move(default_value);
    }
    
    // ValueOrElse: Get value or compute default lazily
    // Usage: auto val = result.ValueOrElse([]() { return createDefault(); });
    template<typename F>
    ScriptValue ValueOrElse(F&& fn) const {
        return IsOk() ? Value() : fn();
    }
    
    // Match: Pattern match on result (visitor pattern)
    // Usage: result.Match(
    //     [](const ScriptValue& v) { return handleValue(v); },
    //     [](const ScriptError& e) { return handleError(e); }
    // );
    template<typename OkFn, typename ErrFn>
    auto Match(OkFn&& ok_fn, ErrFn&& err_fn) const 
        -> decltype(ok_fn(std::declval<const ScriptValue&>())) {
        if (IsOk()) {
            return ok_fn(Value());
        }
        return err_fn(Error());
    }
    
    // Inspect: Side-effect on value without consuming (for debugging/logging)
    // Usage: result.Inspect([](const ScriptValue& v) { LOG_DEBUG(v.ToString()); })
    template<typename F>
    const ScriptResult& Inspect(F&& fn) const {
        if (IsOk()) {
            fn(Value());
        }
        return *this;
    }
    
    // InspectError: Side-effect on error without consuming
    template<typename F>
    const ScriptResult& InspectError(F&& fn) const {
        if (IsError()) {
            fn(Error());
        }
        return *this;
    }

private:
    std::variant<std::monostate, ScriptValue, ScriptError> data_;
};

} // namespace experiments
