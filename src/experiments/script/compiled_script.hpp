#pragma once

/**
 * compiled_script.hpp - Precompiled Script with Bytecode Caching
 * 
 * CompiledScript holds a V8 compiled script for repeated execution.
 * Supports bytecode caching for ~10x faster subsequent loads.
 * 
 * Usage:
 *   // First run - compile and cache
 *   auto script = env->Compile("code", "name");
 *   auto cache = script->GetCachedData();  // Save to disk
 *   
 *   // Later - load from cache
 *   auto script = CompiledScript::FromCachedData(env, cache, "code", "name");
 */

#include <string>
#include <memory>
#include <chrono>
#include <vector>
#include "script_result.hpp"

namespace v8 {
    class Isolate;
    template<class T> class Global;
    class Script;
    class UnboundScript;
}

namespace experiments {

class ScriptEnvironment;

class CompiledScript : public std::enable_shared_from_this<CompiledScript> {
public:
    CompiledScript(ScriptEnvironment* env, const std::string& name);
    ~CompiledScript();
    
    CompiledScript(const CompiledScript&) = delete;
    CompiledScript& operator=(const CompiledScript&) = delete;
    
    // Run the compiled script
    ScriptResult Run(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Script info
    std::string GetName() const { return name_; }
    std::string GetSource() const { return source_; }
    bool IsValid() const { return script_ptr_ != nullptr; }
    
    //=========================================================================
    // Bytecode Caching
    //=========================================================================
    
    // Serialize compiled bytecode for disk storage
    // Returns empty vector if script is invalid or caching failed
    std::vector<uint8_t> GetCachedData() const;
    
    // Create from cached bytecode (much faster than recompiling)
    // source must match the original source used to create the cache
    static std::shared_ptr<CompiledScript> FromCachedData(
        ScriptEnvironment* env,
        const std::vector<uint8_t>& cached_data,
        const std::string& source,
        const std::string& name = "");
    
    // Check if cache was rejected (source changed, V8 version mismatch, etc)
    bool WasCacheRejected() const { return cache_rejected_; }
    
    // For internal use by ScriptEnvironment
    void SetScriptPtr(void* ptr) { script_ptr_ = ptr; }
    void* GetScriptPtr() const { return script_ptr_; }
    void SetSource(const std::string& src) { source_ = src; }
    void SetCacheRejected(bool rejected) { cache_rejected_ = rejected; }
    
private:
    ScriptEnvironment* env_;
    std::string name_;
    std::string source_;
    void* script_ptr_ = nullptr;  // v8::Global<v8::Script>*
    bool cache_rejected_ = false;
};

using CompiledScriptPtr = std::shared_ptr<CompiledScript>;

} // namespace experiments
