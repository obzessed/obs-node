/**
 * script_result.cpp - ScriptResult Implementation
 * 
 * Contains the V8-dependent implementation of ScriptResult.
 */

#include "script_result.hpp"
#include <node/v8.h>

namespace experiments {

//=============================================================================
// ScriptResult::Impl - Holds the actual v8::Global
//=============================================================================

struct ScriptResult::Impl {
    v8::Global<v8::Value> value;
    v8::Global<v8::Context> context;  // Store the context for use from other threads
    
    Impl() = default;
    ~Impl() { 
        value.Reset();
        context.Reset();
    }
    
    // Move
    Impl(Impl&& other) noexcept 
        : value(std::move(other.value))
        , context(std::move(other.context)) {}
    Impl& operator=(Impl&& other) noexcept {
        if (this != &other) {
            value = std::move(other.value);
            context = std::move(other.context);
        }
        return *this;
    }
};

//=============================================================================
// ScriptResult Implementation
//=============================================================================

ScriptResult::ScriptResult(v8::Isolate* isolate)
    : isolate_(isolate)
    , has_value_(false)
    , impl_(new Impl()) {}

ScriptResult::~ScriptResult() {
    delete impl_;
}

ScriptResult::ScriptResult(ScriptResult&& other) noexcept
    : isolate_(other.isolate_)
    , has_value_(other.has_value_)
    , impl_(other.impl_)
    , string_result_(std::move(other.string_result_)) {
    other.isolate_ = nullptr;
    other.has_value_ = false;
    other.impl_ = nullptr;
}

ScriptResult& ScriptResult::operator=(ScriptResult&& other) noexcept {
    if (this != &other) {
        delete impl_;
        
        isolate_ = other.isolate_;
        has_value_ = other.has_value_;
        impl_ = other.impl_;
        string_result_ = std::move(other.string_result_);
        
        other.isolate_ = nullptr;
        other.has_value_ = false;
        other.impl_ = nullptr;
    }
    return *this;
}

ScriptResult ScriptResult::Create(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    ScriptResult result(isolate);
    result.SetValue(value);
    return result;
}

void ScriptResult::SetValue(v8::Local<v8::Value> value) {
    if (!isolate_ || !impl_) return;
    impl_->value.Reset(isolate_, value);
    // Capture the current context for use from other threads
    v8::Local<v8::Context> ctx = isolate_->GetCurrentContext();
    if (!ctx.IsEmpty()) {
        impl_->context.Reset(isolate_, ctx);
    }
    has_value_ = true;
}

void ScriptResult::Reset() {
    if (impl_) {
        impl_->value.Reset();
    }
    has_value_ = false;
}

// Template implementations need V8 headers, so define explicitly in cpp

template<typename F>
auto ScriptResult::WithValue(F&& func) -> std::invoke_result_t<F, v8::Local<v8::Value>> {
    if (!HasValue()) {
        throw std::runtime_error("ScriptResult::WithValue called on empty result");
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    return func(local);
}

template<typename F>
auto ScriptResult::WithValueAndContext(F&& func) 
    -> std::invoke_result_t<F, v8::Local<v8::Value>, v8::Local<v8::Context>> {
    if (!HasValue()) {
        throw std::runtime_error("ScriptResult::WithValueAndContext called on empty result");
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = isolate_->GetCurrentContext();
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    return func(local, context);
}

std::string ScriptResult::ToString() const {
    if (!HasValue()) return string_result_;
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    // Use the stored context (captured when value was set)
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) {
        // Fallback to string_result_ if no context available
        return string_result_;
    }
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    
    if (local.IsEmpty() || local->IsUndefined()) {
        return "";
    }
    
    v8::Local<v8::String> str;
    if (!local->ToString(context).ToLocal(&str)) {
        return "";
    }
    
    v8::String::Utf8Value utf8(isolate_, str);
    return *utf8 ? *utf8 : "";
}

std::optional<double> ScriptResult::ToNumber() const {
    if (!HasValue()) return std::nullopt;
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    
    if (!local->IsNumber()) return std::nullopt;
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) return std::nullopt;
    v8::Context::Scope context_scope(context);
    
    v8::Maybe<double> maybe = local->NumberValue(context);
    if (maybe.IsNothing()) return std::nullopt;
    
    return maybe.FromJust();
}

std::optional<bool> ScriptResult::ToBool() const {
    if (!HasValue()) return std::nullopt;
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    return local->BooleanValue(isolate_);
}

std::optional<int64_t> ScriptResult::ToInt64() const {
    if (!HasValue()) return std::nullopt;
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    
    if (!local->IsNumber()) return std::nullopt;
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) return std::nullopt;
    v8::Context::Scope context_scope(context);
    
    v8::Maybe<int64_t> maybe = local->IntegerValue(context);
    if (maybe.IsNothing()) return std::nullopt;
    
    return maybe.FromJust();
}

bool ScriptResult::IsString() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsString();
}

bool ScriptResult::IsNumber() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsNumber();
}

bool ScriptResult::IsBoolean() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsBoolean();
}

bool ScriptResult::IsObject() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsObject();
}

bool ScriptResult::IsArray() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsArray();
}

bool ScriptResult::IsFunction() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsFunction();
}

bool ScriptResult::IsNull() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsNull();
}

bool ScriptResult::IsUndefined() const {
    if (!HasValue()) return false;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsUndefined();
}

bool ScriptResult::IsNullOrUndefined() const {
    if (!HasValue()) return true;
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    return impl_->value.Get(isolate_)->IsNullOrUndefined();
}

//=============================================================================
// Function Calling
//=============================================================================

ScriptResult ScriptResult::Call() {
    return Call({});
}

ScriptResult ScriptResult::Call(const std::vector<ScriptResult*>& args) {
    if (!HasValue()) {
        throw std::runtime_error("ScriptResult::Call: no value");
    }
    if (!IsFunction()) {
        throw std::runtime_error("ScriptResult::Call: value is not a function");
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) {
        throw std::runtime_error("ScriptResult::Call: no context available");
    }
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    v8::Local<v8::Function> func = local.As<v8::Function>();
    
    // Convert arguments
    std::vector<v8::Local<v8::Value>> v8_args;
    v8_args.reserve(args.size());
    for (ScriptResult* arg : args) {
        if (arg && arg->HasValue() && arg->isolate_ == isolate_) {
            v8_args.push_back(arg->impl_->value.Get(isolate_));
        } else {
            v8_args.push_back(v8::Undefined(isolate_));
        }
    }
    
    v8::TryCatch try_catch(isolate_);
    v8::MaybeLocal<v8::Value> maybe_result = func->Call(
        context,
        context->Global(),  // 'this' = global
        static_cast<int>(v8_args.size()),
        v8_args.empty() ? nullptr : v8_args.data()
    );
    
    if (try_catch.HasCaught()) {
        v8::String::Utf8Value error(isolate_, try_catch.Exception());
        throw std::runtime_error(std::string("ScriptResult::Call error: ") + (*error ? *error : "unknown"));
    }
    
    v8::Local<v8::Value> result;
    if (!maybe_result.ToLocal(&result)) {
        return ScriptResult(); // Empty result
    }
    
    return ScriptResult::Create(isolate_, result);
}

ScriptResult ScriptResult::CallMethod(const std::string& method_name, const std::vector<ScriptResult*>& args) {
    if (!HasValue()) {
        throw std::runtime_error("ScriptResult::CallMethod: no value");
    }
    if (!IsObject()) {
        throw std::runtime_error("ScriptResult::CallMethod: value is not an object");
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) {
        throw std::runtime_error("ScriptResult::CallMethod: no context available");
    }
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    v8::Local<v8::Object> obj = local.As<v8::Object>();
    
    // Get the method
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate_, method_name.c_str()).ToLocalChecked();
    v8::MaybeLocal<v8::Value> maybe_method = obj->Get(context, key);
    
    v8::Local<v8::Value> method_value;
    if (!maybe_method.ToLocal(&method_value) || !method_value->IsFunction()) {
        throw std::runtime_error("ScriptResult::CallMethod: method '" + method_name + "' not found or not a function");
    }
    
    v8::Local<v8::Function> func = method_value.As<v8::Function>();
    
    // Convert arguments
    std::vector<v8::Local<v8::Value>> v8_args;
    v8_args.reserve(args.size());
    for (ScriptResult* arg : args) {
        if (arg && arg->HasValue() && arg->isolate_ == isolate_) {
            v8_args.push_back(arg->impl_->value.Get(isolate_));
        } else {
            v8_args.push_back(v8::Undefined(isolate_));
        }
    }
    
    v8::TryCatch try_catch(isolate_);
    v8::MaybeLocal<v8::Value> maybe_result = func->Call(
        context,
        obj,  // 'this' = the object
        static_cast<int>(v8_args.size()),
        v8_args.empty() ? nullptr : v8_args.data()
    );
    
    if (try_catch.HasCaught()) {
        v8::String::Utf8Value error(isolate_, try_catch.Exception());
        throw std::runtime_error(std::string("ScriptResult::CallMethod error: ") + (*error ? *error : "unknown"));
    }
    
    v8::Local<v8::Value> result;
    if (!maybe_result.ToLocal(&result)) {
        return ScriptResult();
    }
    
    return ScriptResult::Create(isolate_, result);
}

//=============================================================================
// Object Property Access
//=============================================================================

ScriptResult ScriptResult::Get(const std::string& key) {
    if (!HasValue() || !IsObject()) {
        return ScriptResult();
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) return ScriptResult();
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    v8::Local<v8::Object> obj = local.As<v8::Object>();
    
    v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
    v8::MaybeLocal<v8::Value> maybe_value = obj->Get(context, v8_key);
    
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return ScriptResult();
    }
    
    return ScriptResult::Create(isolate_, value);
}

ScriptResult ScriptResult::Get(uint32_t index) {
    if (!HasValue() || !IsArray()) {
        return ScriptResult();
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) return ScriptResult();
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    v8::Local<v8::Array> arr = local.As<v8::Array>();
    
    v8::MaybeLocal<v8::Value> maybe_value = arr->Get(context, index);
    
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return ScriptResult();
    }
    
    return ScriptResult::Create(isolate_, value);
}

bool ScriptResult::Set(const std::string& key, ScriptResult& value) {
    if (!HasValue() || !IsObject()) {
        return false;
    }
    if (!value.HasValue() || value.isolate_ != isolate_) {
        return false;
    }
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Context> context = impl_->context.Get(isolate_);
    if (context.IsEmpty()) return false;
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    v8::Local<v8::Object> obj = local.As<v8::Object>();
    
    v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
    v8::Local<v8::Value> v8_value = value.impl_->value.Get(isolate_);
    
    v8::Maybe<bool> result = obj->Set(context, v8_key, v8_value);
    return result.FromMaybe(false);
}

std::optional<uint32_t> ScriptResult::Length() const {
    if (!HasValue()) return std::nullopt;
    
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    
    v8::Local<v8::Value> local = impl_->value.Get(isolate_);
    
    if (local->IsArray()) {
        return local.As<v8::Array>()->Length();
    }
    
    if (local->IsString()) {
        return local.As<v8::String>()->Length();
    }
    
    return std::nullopt;
}

//=============================================================================
// Static Value Creators
//=============================================================================

ScriptResult ScriptResult::FromNumber(v8::Isolate* isolate, double value) {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Number> num = v8::Number::New(isolate, value);
    return ScriptResult::Create(isolate, num);
}

ScriptResult ScriptResult::FromString(v8::Isolate* isolate, const std::string& value) {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::String> str = v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
    return ScriptResult::Create(isolate, str);
}

ScriptResult ScriptResult::FromBool(v8::Isolate* isolate, bool value) {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Boolean> b = v8::Boolean::New(isolate, value);
    return ScriptResult::Create(isolate, b);
}

ScriptResult ScriptResult::Undefined(v8::Isolate* isolate) {
    v8::HandleScope handle_scope(isolate);
    return ScriptResult::Create(isolate, v8::Undefined(isolate));
}

ScriptResult ScriptResult::Null(v8::Isolate* isolate) {
    v8::HandleScope handle_scope(isolate);
    return ScriptResult::Create(isolate, v8::Null(isolate));
}

// Explicit template instantiations for common callback types
template auto ScriptResult::WithValue<std::function<bool(v8::Local<v8::Value>)>>(
    std::function<bool(v8::Local<v8::Value>)>&&) -> bool;
template auto ScriptResult::WithValue<std::function<std::string(v8::Local<v8::Value>)>>(
    std::function<std::string(v8::Local<v8::Value>)>&&) -> std::string;
template auto ScriptResult::WithValue<std::function<double(v8::Local<v8::Value>)>>(
    std::function<double(v8::Local<v8::Value>)>&&) -> double;
template auto ScriptResult::WithValue<std::function<void(v8::Local<v8::Value>)>>(
    std::function<void(v8::Local<v8::Value>)>&&) -> void;

} // namespace experiments

