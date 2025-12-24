/**
 * compiled_script.cpp - CompiledScript Implementation with Bytecode Caching
 */

#include "compiled_script.hpp"
#include "environment.hpp"
#include "node/node.h"

#include <node/v8.h>

namespace experiments {

CompiledScript::CompiledScript(ScriptEnvironment* env, const std::string& name)
    : env_(env), name_(name) {}

CompiledScript::~CompiledScript() {
    if (script_ptr_) {
        auto* global = static_cast<v8::Global<v8::Script>*>(script_ptr_);
        delete global;
        script_ptr_ = nullptr;
    }
}

ScriptResult CompiledScript::Run(std::chrono::milliseconds timeout) {
    if (!IsValid() || !env_) {
        return ScriptResult::Err(ErrorCode::InternalError, "Invalid compiled script");
    }
    
    return env_->RunCompiledScript(shared_from_this(), timeout);
}

std::vector<uint8_t> CompiledScript::GetCachedData() const {
    if (!IsValid() || !env_) {
        return {};
    }
    
    auto* setup = env_->GetSetup();
    if (!setup) {
        return {};
    }
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = setup->context();
    v8::Context::Scope context_scope(context);
    
    auto* script_global = static_cast<v8::Global<v8::Script>*>(script_ptr_);
    v8::Local<v8::Script> script = script_global->Get(isolate);
    
    // Get the unbound script for caching
    v8::Local<v8::UnboundScript> unbound = script->GetUnboundScript();
    
    // Create code cache
    v8::ScriptCompiler::CachedData* cache_data = 
        v8::ScriptCompiler::CreateCodeCache(unbound);
    
    if (!cache_data || cache_data->length == 0) {
        delete cache_data;
        return {};
    }
    
    // Copy to vector
    std::vector<uint8_t> result(cache_data->data, cache_data->data + cache_data->length);
    
    delete cache_data;
    return result;
}

std::shared_ptr<CompiledScript> CompiledScript::FromCachedData(
    ScriptEnvironment* env,
    const std::vector<uint8_t>& cached_data,
    const std::string& source,
    const std::string& name)
{
    if (!env || cached_data.empty() || source.empty()) {
        return nullptr;
    }
    
    auto* setup = env->GetSetup();
    if (!setup) {
        return nullptr;
    }
    
    v8::Isolate* isolate = setup->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = setup->context();
    v8::Context::Scope context_scope(context);
    
    v8::TryCatch try_catch(isolate);
    
    // Create source string
    v8::Local<v8::String> v8_source;
    if (!v8::String::NewFromUtf8(isolate, source.c_str()).ToLocal(&v8_source)) {
        return nullptr;
    }
    
    // Create cached data (V8 takes ownership)
    v8::ScriptCompiler::CachedData* cache = new v8::ScriptCompiler::CachedData(
        cached_data.data(),
        static_cast<int>(cached_data.size()),
        v8::ScriptCompiler::CachedData::BufferNotOwned
    );
    
    // Create source with cache
    v8::ScriptCompiler::Source script_source(v8_source, cache);
    
    // Compile with cache consumption
    v8::Local<v8::Script> compiled;
    if (!v8::ScriptCompiler::Compile(
            context,
            &script_source,
            v8::ScriptCompiler::kConsumeCodeCache).ToLocal(&compiled)) {
        return nullptr;
    }
    
    // Create CompiledScript
    std::string script_name = name.empty() ? "cached_script" : name;
    auto result = std::make_shared<CompiledScript>(env, script_name);
    result->SetSource(source);
    result->SetCacheRejected(cache->rejected);
    
    // Store the compiled script
    auto* script_global = new v8::Global<v8::Script>(isolate, compiled);
    result->SetScriptPtr(script_global);
    
    return result;
}

} // namespace experiments
