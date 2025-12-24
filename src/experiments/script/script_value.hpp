#pragma once

/**
 * script_value.hpp - Safe V8 Value Wrapper (Environment-Delegated)
 * 
 * ScriptValue is a handle to a V8 value stored in the ScriptEnvironment's
 * value registry. All V8 operations are delegated to the environment.
 */

#include <string>
#include <optional>
#include <vector>
#include <map>
#include <cstdint>

namespace experiments {

// Forward declaration
class ScriptEnvironment;

/**
 * ScriptValue - Handle to a V8 value in the environment's registry
 * 
 * This is a lightweight handle that delegates all V8 operations to
 * the ScriptEnvironment. Safe to use from any thread.
 * 
 * Lifetime: Must not outlive the ScriptEnvironment that created it.
 */
class ScriptValue {
public:
    using ValueId = uint64_t;
    static constexpr ValueId INVALID_ID = 0;
    
    ScriptValue() = default;
    ScriptValue(ScriptEnvironment* env, ValueId id);
    ~ScriptValue();
    
    // Move semantics (releases old value, takes ownership of new)
    ScriptValue(ScriptValue&& other) noexcept;
    ScriptValue& operator=(ScriptValue&& other) noexcept;
    
    // Copy increments refcount
    ScriptValue(const ScriptValue& other);
    ScriptValue& operator=(const ScriptValue& other);
    
    // State checks
    bool HasValue() const { return env_ != nullptr && value_id_ != INVALID_ID; }
    bool IsEmpty() const { return !HasValue(); }
    ValueId GetValueId() const { return value_id_; }
    ScriptEnvironment* GetEnvironment() const { return env_; }
    
    // Type checks (delegated to environment)
    bool IsString() const;
    bool IsNumber() const;
    bool IsBoolean() const;
    bool IsObject() const;
    bool IsArray() const;
    bool IsFunction() const;
    bool IsNull() const;
    bool IsUndefined() const;
    bool IsNullOrUndefined() const;
    
    // Value extraction (copies out primitives, safe from any thread)
    std::string ToString() const;
    std::optional<double> ToNumber() const;
    std::optional<bool> ToBool() const;
    std::optional<int64_t> ToInt64() const;
    
    // Object/Array access
    ScriptValue Get(const std::string& key);
    ScriptValue Get(uint32_t index);
    bool Set(const std::string& key, ScriptValue& value);
    std::optional<uint32_t> Length() const;
    
    // Function calling
    ScriptValue Call();
    ScriptValue Call(const std::vector<ScriptValue*>& args);
    ScriptValue CallMethod(const std::string& method, const std::vector<ScriptValue*>& args = {});
    
    // Operator access (syntactic sugar for Get)
    ScriptValue operator[](const std::string& key) { return Get(key); }
    ScriptValue operator[](const char* key) { return Get(std::string(key)); }
    ScriptValue operator[](uint32_t index) { return Get(index); }
    ScriptValue operator[](int index) { return Get(static_cast<uint32_t>(index)); }
    
    // Template value extraction
    template<typename T>
    std::optional<T> As() const;
    
    // Iterator for array values
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = ScriptValue;
        using difference_type = std::ptrdiff_t;
        using pointer = ScriptValue*;
        using reference = ScriptValue;
        
        Iterator(ScriptValue* arr, uint32_t index) : arr_(arr), index_(index) {}
        
        reference operator*() { return arr_->Get(index_); }
        Iterator& operator++() { ++index_; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++index_; return tmp; }
        bool operator==(const Iterator& other) const { return index_ == other.index_; }
        bool operator!=(const Iterator& other) const { return index_ != other.index_; }
        
    private:
        ScriptValue* arr_;
        uint32_t index_;
    };
    
    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, Length().value_or(0)); }
    
    // String result for backward compat
    void SetStringResult(std::string str) { string_result_ = std::move(str); }
    const std::string& GetStringResult() const { return string_result_; }
    
    // JSON interop
    std::string ToJsonString() const;
    
    // Type conversion for containers
    template<typename T>
    std::vector<T> ToVector() const;
    
    std::vector<std::string> Keys() const;  // Get object keys
    
    // Static factory methods (implemented in cpp)
    static ScriptValue CreateArray(ScriptEnvironment* env, size_t length = 0);
    static ScriptValue CreateObject(ScriptEnvironment* env);
    
    // Release value (decrements refcount)
    void Release();

private:
    ScriptEnvironment* env_ = nullptr;
    ValueId value_id_ = INVALID_ID;
    std::string string_result_;  // Backward compat
};

// Template specializations for As<T>()
template<> inline std::optional<double> ScriptValue::As<double>() const { return ToNumber(); }
template<> inline std::optional<int> ScriptValue::As<int>() const { 
    auto n = ToNumber(); 
    return n ? std::optional<int>(static_cast<int>(*n)) : std::nullopt; 
}
template<> inline std::optional<int64_t> ScriptValue::As<int64_t>() const { return ToInt64(); }
template<> inline std::optional<bool> ScriptValue::As<bool>() const { return ToBool(); }
template<> inline std::optional<std::string> ScriptValue::As<std::string>() const { 
    return HasValue() ? std::optional<std::string>(ToString()) : std::nullopt; 
}

// Template implementation for ToVector
template<typename T>
std::vector<T> ScriptValue::ToVector() const {
    std::vector<T> result;
    if (!HasValue() || !IsArray()) return result;
    
    auto len = Length();
    if (!len) return result;
    
    result.reserve(*len);
    for (uint32_t i = 0; i < *len; i++) {
        // Access element through const_cast since Get is non-const
        auto elem = const_cast<ScriptValue*>(this)->Get(i);
        if (auto val = elem.As<T>()) {
            result.push_back(std::move(*val));
        }
    }
    return result;
}

} // namespace experiments
