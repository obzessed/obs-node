#pragma once

/**
 * script_result.hpp - Safe V8 Value Wrapper
 * 
 * Wraps v8::Global<v8::Value> with safety mechanisms ensuring proper
 * V8 scoping (Locker, HandleScope, Context) when accessing the value.
 */

#include <string>
#include <optional>
#include <functional>
#include <type_traits>
#include <stdexcept>
#include <vector>

// Forward declarations - actual V8 headers needed in implementation
namespace v8 {
    class Isolate;
    class Context;
    class Value;
    template<class T> class Local;
    template<class T> class Global;
}

namespace experiments {

/**
 * ScriptResult - Safe wrapper for V8 values
 * 
 * Stores a v8::Global<v8::Value> that persists beyond HandleScope.
 * Provides safe access methods that ensure proper V8 scoping.
 * 
 * Thread Safety:
 * - The object itself can be moved between threads
 * - Access to the value (WithValue, ToString, etc.) acquires v8::Locker
 * - Only one thread can access the isolate at a time
 * 
 * Lifetime:
 * - Must not outlive the ScriptEnvironment that created it
 * - The isolate pointer becomes invalid after environment destruction
 */
class ScriptResult {
public:
    ScriptResult() = default;
    ~ScriptResult();
    
    // Move-only semantics (Global handles shouldn't be copied)
    ScriptResult(ScriptResult&& other) noexcept;
    ScriptResult& operator=(ScriptResult&& other) noexcept;
    ScriptResult(const ScriptResult&) = delete;
    ScriptResult& operator=(const ScriptResult&) = delete;
    
    // Factory - called from RunScript with active V8 context
    static ScriptResult Create(v8::Isolate* isolate, v8::Local<v8::Value> value);
    
    // State checks
    bool HasValue() const { return isolate_ != nullptr && has_value_; }
    bool IsEmpty() const { return !HasValue(); }
    v8::Isolate* GetIsolate() const { return isolate_; }
    
    /**
     * Access the raw V8 value with proper scoping.
     * 
     * Usage:
     *   result.WithValue([](v8::Local<v8::Value> val) {
     *       // Use val here - scopes are active
     *       return val->IsNumber();
     *   });
     * 
     * Warning: This acquires v8::Locker which blocks the env thread.
     */
    template<typename F>
    auto WithValue(F&& func) -> std::invoke_result_t<F, v8::Local<v8::Value>>;
    
    /**
     * Access with context (for operations that need it)
     */
    template<typename F>
    auto WithValueAndContext(F&& func) -> std::invoke_result_t<F, v8::Local<v8::Value>, v8::Local<v8::Context>>;
    
    // Convenience extractors (thread-safe, copy out primitives)
    std::string ToString() const;
    std::optional<double> ToNumber() const;
    std::optional<bool> ToBool() const;
    std::optional<int64_t> ToInt64() const;
    
    // Type checks
    bool IsString() const;
    bool IsNumber() const;
    bool IsBoolean() const;
    bool IsObject() const;
    bool IsArray() const;
    bool IsFunction() const;
    bool IsNull() const;
    bool IsUndefined() const;
    bool IsNullOrUndefined() const;
    
    //=========================================================================
    // Function Calling
    //=========================================================================
    
    /**
     * Call this value as a function with no arguments.
     * Returns a new ScriptResult containing the return value.
     * Throws if this is not a function.
     */
    ScriptResult Call();
    
    /**
     * Call this value as a function with arguments specified as ScriptResults.
     * @param args Vector of ScriptResult arguments
     * @return ScriptResult containing the return value
     */
    ScriptResult Call(const std::vector<ScriptResult*>& args);
    
    /**
     * Call a method on this object.
     * @param method_name Name of the method to call
     * @param args Vector of ScriptResult arguments
     * @return ScriptResult containing the return value
     */
    ScriptResult CallMethod(const std::string& method_name, const std::vector<ScriptResult*>& args = {});
    
    //=========================================================================
    // Object Property Access
    //=========================================================================
    
    /**
     * Get a property from this object.
     * @param key Property name
     * @return ScriptResult containing the property value (may be undefined)
     */
    ScriptResult Get(const std::string& key);
    
    /**
     * Get an element from this array by index.
     * @param index Array index
     * @return ScriptResult containing the element (may be undefined)
     */
    ScriptResult Get(uint32_t index);
    
    /**
     * Set a property on this object.
     * @param key Property name
     * @param value Value to set
     * @return true if successful
     */
    bool Set(const std::string& key, ScriptResult& value);
    
    /**
     * Get the length of an array or string.
     * @return Length, or nullopt if not applicable
     */
    std::optional<uint32_t> Length() const;
    
    //=========================================================================
    // Static Value Creators (requires active V8 context)
    //=========================================================================
    
    /**
     * Create a ScriptResult from a primitive value.
     * Note: Must be called from within WithValueAndContext or environment thread.
     */
    static ScriptResult FromNumber(v8::Isolate* isolate, double value);
    static ScriptResult FromString(v8::Isolate* isolate, const std::string& value);
    static ScriptResult FromBool(v8::Isolate* isolate, bool value);
    static ScriptResult Undefined(v8::Isolate* isolate);
    static ScriptResult Null(v8::Isolate* isolate);
    
    // Also store string representation for backward compatibility
    void SetStringResult(std::string str) { string_result_ = std::move(str); }
    const std::string& GetStringResult() const { return string_result_; }

private:
    // Private constructor - use Create() factory
    ScriptResult(v8::Isolate* isolate);
    
    // Set value (called by Create, needs V8 context active)
    void SetValue(v8::Local<v8::Value> value);
    
    // Reset/clear
    void Reset();
    
    v8::Isolate* isolate_ = nullptr;
    bool has_value_ = false;
    
    // v8::Global storage - actual V8 header needed
    // We use a pointer to avoid requiring V8 headers in this header
    struct Impl;
    Impl* impl_ = nullptr;
    
    // Backward compat string result
    std::string string_result_;
};

} // namespace experiments
