/**
 * script_value.cpp - ScriptValue Implementation (Environment-Delegated)
 */

#include "script_value.hpp"
#include "environment.hpp"

namespace experiments {

ScriptValue::ScriptValue(ScriptEnvironment* env, ValueId id)
    : env_(env), value_id_(id) {}

ScriptValue::~ScriptValue() {
    Release();
}

ScriptValue::ScriptValue(ScriptValue&& other) noexcept
    : env_(other.env_)
    , value_id_(other.value_id_)
    , string_result_(std::move(other.string_result_)) {
    other.env_ = nullptr;
    other.value_id_ = INVALID_ID;
}

ScriptValue& ScriptValue::operator=(ScriptValue&& other) noexcept {
    if (this != &other) {
        Release();
        env_ = other.env_;
        value_id_ = other.value_id_;
        string_result_ = std::move(other.string_result_);
        other.env_ = nullptr;
        other.value_id_ = INVALID_ID;
    }
    return *this;
}

ScriptValue::ScriptValue(const ScriptValue& other)
    : env_(other.env_)
    , value_id_(other.value_id_)
    , string_result_(other.string_result_) {
    // Increment refcount for the shared value
    if (env_ && value_id_ != INVALID_ID) {
        env_->AddValueRef(value_id_);
    }
}

ScriptValue& ScriptValue::operator=(const ScriptValue& other) {
    if (this != &other) {
        Release();
        env_ = other.env_;
        value_id_ = other.value_id_;
        string_result_ = other.string_result_;
        // Increment refcount for the new value
        if (env_ && value_id_ != INVALID_ID) {
            env_->AddValueRef(value_id_);
        }
    }
    return *this;
}

void ScriptValue::Release() {
    if (env_ && value_id_ != INVALID_ID) {
        env_->ReleaseValue(value_id_);
        value_id_ = INVALID_ID;
        env_ = nullptr;
    }
}

// Type checks
bool ScriptValue::IsString() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::String;
}

bool ScriptValue::IsNumber() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Number;
}

bool ScriptValue::IsBoolean() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Boolean;
}

bool ScriptValue::IsObject() const {
    if (!HasValue()) return false;
    auto type = env_->GetValueType(value_id_);
    return type == ScriptEnvironment::ValueType::Object ||
           type == ScriptEnvironment::ValueType::Array ||
           type == ScriptEnvironment::ValueType::Function;
}

bool ScriptValue::IsArray() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Array;
}

bool ScriptValue::IsFunction() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Function;
}

bool ScriptValue::IsNull() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Null;
}

bool ScriptValue::IsUndefined() const {
    if (!HasValue()) return false;
    return env_->GetValueType(value_id_) == ScriptEnvironment::ValueType::Undefined;
}

bool ScriptValue::IsNullOrUndefined() const {
    if (!HasValue()) return true;
    auto type = env_->GetValueType(value_id_);
    return type == ScriptEnvironment::ValueType::Null ||
           type == ScriptEnvironment::ValueType::Undefined;
}

// Value extraction
std::string ScriptValue::ToString() const {
    if (!HasValue()) return string_result_;
    return env_->ValueToString(value_id_);
}

std::optional<double> ScriptValue::ToNumber() const {
    if (!HasValue()) return std::nullopt;
    return env_->ValueToNumber(value_id_);
}

std::optional<bool> ScriptValue::ToBool() const {
    if (!HasValue()) return std::nullopt;
    return env_->ValueToBool(value_id_);
}

std::optional<int64_t> ScriptValue::ToInt64() const {
    if (!HasValue()) return std::nullopt;
    return env_->ValueToInt64(value_id_);
}

std::string ScriptValue::ToJsonString() const {
    if (!HasValue()) return "null";
    return env_->ValueToJson(value_id_);
}

// Object/Array access
ScriptValue ScriptValue::Get(const std::string& key) {
    if (!HasValue()) return ScriptValue();
    auto id = env_->GetProperty(value_id_, key);
    return ScriptValue(env_, id);
}

ScriptValue ScriptValue::Get(uint32_t index) {
    if (!HasValue()) return ScriptValue();
    auto id = env_->GetArrayElement(value_id_, index);
    return ScriptValue(env_, id);
}

bool ScriptValue::Set(const std::string& key, ScriptValue& value) {
    if (!HasValue() || !value.HasValue()) return false;
    if (env_ != value.env_) return false;
    return env_->SetProperty(value_id_, key, value.value_id_);
}

std::optional<uint32_t> ScriptValue::Length() const {
    if (!HasValue()) return std::nullopt;
    return env_->GetLength(value_id_);
}

// Function calling
ScriptValue ScriptValue::Call() {
    return Call({});
}

ScriptValue ScriptValue::Call(const std::vector<ScriptValue*>& args) {
    if (!HasValue()) return ScriptValue();
    
    std::vector<ScriptEnvironment::ValueId> arg_ids;
    arg_ids.reserve(args.size());
    for (auto* arg : args) {
        if (arg && arg->HasValue() && arg->env_ == env_) {
            arg_ids.push_back(arg->value_id_);
        } else {
            arg_ids.push_back(ScriptEnvironment::INVALID_VALUE_ID);
        }
    }
    
    auto result_id = env_->InvokeFunction(value_id_, arg_ids);
    return ScriptValue(env_, result_id);
}

ScriptValue ScriptValue::CallMethod(const std::string& method, const std::vector<ScriptValue*>& args) {
    if (!HasValue()) return ScriptValue();
    
    std::vector<ScriptEnvironment::ValueId> arg_ids;
    arg_ids.reserve(args.size());
    for (auto* arg : args) {
        if (arg && arg->HasValue() && arg->env_ == env_) {
            arg_ids.push_back(arg->value_id_);
        } else {
            arg_ids.push_back(ScriptEnvironment::INVALID_VALUE_ID);
        }
    }
    
    auto result_id = env_->CallMethod(value_id_, method, arg_ids);
    return ScriptValue(env_, result_id);
}

std::vector<std::string> ScriptValue::Keys() const {
    std::vector<std::string> result;
    if (!HasValue()) return result;
    return env_->GetObjectKeys(value_id_);
}

ScriptValue ScriptValue::CreateArray(ScriptEnvironment* env, size_t length) {
    if (!env) return ScriptValue();
    auto id = env->CreateArray(length);
    return ScriptValue(env, id);
}

ScriptValue ScriptValue::CreateObject(ScriptEnvironment* env) {
    if (!env) return ScriptValue();
    auto id = env->CreateObject();
    return ScriptValue(env, id);
}

} // namespace experiments

