/**
 * sandbox_context.cpp - SandboxContext Implementation
 */

#include "sandbox_context.hpp"
#include "environment.hpp"
#include "node/node.h"

#include <node/v8.h>

namespace experiments {

std::atomic<SandboxContext::ContextId> SandboxContext::next_id_{1};

SandboxContext::SandboxContext(ScriptEnvironment* env, const std::string& name)
    : env_(env)
    , name_(name.empty() ? "sandbox_" + std::to_string(next_id_.load()) : name)
    , id_(next_id_++) {}

SandboxContext::~SandboxContext() {
    if (context_ptr_) {
        auto* global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
        delete global;
        context_ptr_ = nullptr;
    }
}

bool SandboxContext::Initialize() {
    return Initialize({});
}

bool SandboxContext::Initialize(const std::map<std::string, ScriptValue>& sandbox) {
    if (!env_ || context_ptr_) return false;
    
    auto* setup = env_->GetSetup();
    if (!setup) return false;
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    // Create a new context with fresh global
    v8::Local<v8::Context> new_context = v8::Context::New(isolate);
    
    if (new_context.IsEmpty()) return false;
    
    // Enter the new context to set up globals
    v8::Context::Scope context_scope(new_context);
    
    // Contextify: copy sandbox values as globals
    v8::Local<v8::Object> global = new_context->Global();
    
    for (const auto& [name, value] : sandbox) {
        if (!value.HasValue()) continue;
        
        // Get the V8 value from the parent context's registry
        auto value_id = value.GetValueId();
        auto* entry = env_->GetValueEntry(value_id);
        if (!entry) continue;
        
        auto* value_global = static_cast<v8::Global<v8::Value>*>(entry->global_ptr);
        v8::Local<v8::Value> v8_val = value_global->Get(isolate);
        
        v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
        global->Set(new_context, key, v8_val).Check();
    }
    
    // Store the context
    context_ptr_ = new v8::Global<v8::Context>(isolate, new_context);
    
    return true;
}

ScriptResult SandboxContext::Run(const std::string& code, std::chrono::milliseconds timeout) {
    if (!IsValid() || !env_) {
        return ScriptResult::Err(ErrorCode::InternalError, "Invalid sandbox context");
    }
    
    auto* setup = env_->GetSetup();
    if (!setup) {
        return ScriptResult::Err(ErrorCode::NotInitialized, "Environment not initialized");
    }
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    v8::TryCatch try_catch(isolate);
    
    // Compile
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(isolate, code.c_str()).ToLocal(&source)) {
        return ScriptResult::Err(ErrorCode::InternalError, "Failed to create source");
    }
    
    v8::Local<v8::Script> compiled;
    if (!v8::Script::Compile(context, source).ToLocal(&compiled)) {
        std::string err_msg = "Compile error";
        if (try_catch.HasCaught()) {
            v8::Local<v8::Message> msg = try_catch.Message();
            if (!msg.IsEmpty()) {
                v8::String::Utf8Value utf8(isolate, msg->Get());
                if (*utf8) err_msg = *utf8;
            }
        }
        return ScriptResult::Err(ErrorCode::CompileError, err_msg);
    }
    
    // Run
    v8::Local<v8::Value> result_val;
    if (!compiled->Run(context).ToLocal(&result_val)) {
        std::string err_msg = "Execution error";
        if (try_catch.HasCaught()) {
            v8::Local<v8::Message> msg = try_catch.Message();
            if (!msg.IsEmpty()) {
                v8::String::Utf8Value utf8(isolate, msg->Get());
                if (*utf8) err_msg = *utf8;
            }
        }
        return ScriptResult::Err(ErrorCode::ExecutionError, err_msg);
    }
    
    // Convert result
    std::string result_str;
    {
        v8::Local<v8::String> str;
        if (result_val->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8) result_str = *utf8;
        }
    }
    
    auto result_id = env_->RegisterValue(&result_val);
    ScriptValue result_value(env_, result_id);
    result_value.SetStringResult(result_str);
    
    return ScriptResult::Ok(std::move(result_value));
}

void SandboxContext::SetGlobal(const std::string& name, ScriptValue value) {
    if (!IsValid() || !value.HasValue() || !env_) return;
    
    auto* setup = env_->GetSetup();
    if (!setup) return;
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    auto* entry = env_->GetValueEntry(value.GetValueId());
    if (!entry) return;
    
    auto* value_global = static_cast<v8::Global<v8::Value>*>(entry->global_ptr);
    v8::Local<v8::Value> v8_val = value_global->Get(isolate);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    context->Global()->Set(context, key, v8_val).Check();
}

void SandboxContext::SetGlobal(const std::string& name, double value) {
    if (!IsValid() || !env_) return;
    
    auto* setup = env_->GetSetup();
    if (!setup) return;
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::Number> val = v8::Number::New(isolate, value);
    context->Global()->Set(context, key, val).Check();
}

void SandboxContext::SetGlobal(const std::string& name, const std::string& value) {
    if (!IsValid() || !env_) return;
    
    auto* setup = env_->GetSetup();
    if (!setup) return;
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::String> val = v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
    context->Global()->Set(context, key, val).Check();
}

void SandboxContext::SetGlobal(const std::string& name, const char* value) {
    SetGlobal(name, std::string(value));
}

void SandboxContext::SetGlobal(const std::string& name, bool value) {
    if (!IsValid() || !env_) return;
    
    auto* setup = env_->GetSetup();
    if (!setup) return;
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::Boolean> val = v8::Boolean::New(isolate, value);
    context->Global()->Set(context, key, val).Check();
}

ScriptValue SandboxContext::GetGlobal(const std::string& name) {
    if (!IsValid() || !env_) return ScriptValue();
    
    auto* setup = env_->GetSetup();
    if (!setup) return ScriptValue();
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    auto* ctx_global = static_cast<v8::Global<v8::Context>*>(context_ptr_);
    v8::Local<v8::Context> context = ctx_global->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::Value> val;
    if (!context->Global()->Get(context, key).ToLocal(&val)) {
        return ScriptValue();
    }
    
    auto id = env_->RegisterValue(&val);
    return ScriptValue(env_, id);
}

void SandboxContext::Contextify(const std::string& name, ScriptValue value) {
    // Contextify is same as SetGlobal with ScriptValue
    // Objects in same isolate share the same reference
    SetGlobal(name, value);
}

} // namespace experiments
