#pragma once

/**
 * sandbox_context.hpp - Sandboxed V8 Context (Node.js vm-style)
 * 
 * SandboxContext creates an isolated execution context within the same isolate.
 * Objects can be shared across contexts since they share the same V8 heap.
 * 
 * Similar to Node.js vm.createContext() and vm.runInContext().
 */

#include <string>
#include <memory>
#include <chrono>
#include <map>
#include "script_result.hpp"
#include "script_value.hpp"

namespace experiments {

class ScriptEnvironment;

/**
 * SandboxContext - An isolated JavaScript execution environment
 * 
 * Features:
 * - Separate global object from main context
 * - Can share objects/functions from parent context (contextify)
 * - Scripts run with sandbox globals, not main globals
 */
class SandboxContext : public std::enable_shared_from_this<SandboxContext> {
public:
    using ContextId = uint64_t;
    
    SandboxContext(ScriptEnvironment* env, const std::string& name = "");
    ~SandboxContext();
    
    SandboxContext(const SandboxContext&) = delete;
    SandboxContext& operator=(const SandboxContext&) = delete;
    
    // Create the V8 context with optional sandbox object
    bool Initialize();
    bool Initialize(const std::map<std::string, ScriptValue>& sandbox);
    
    // Run code in this sandboxed context
    ScriptResult Run(const std::string& code, 
                     std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Set/Get globals in this context
    void SetGlobal(const std::string& name, ScriptValue value);
    void SetGlobal(const std::string& name, double value);
    void SetGlobal(const std::string& name, const std::string& value);
    void SetGlobal(const std::string& name, const char* value);  // Avoid bool ambiguity
    void SetGlobal(const std::string& name, bool value);
    ScriptValue GetGlobal(const std::string& name);
    
    // Contextify: Copy value from parent context to sandbox
    // (References are shared since same isolate)
    void Contextify(const std::string& name, ScriptValue value);
    
    // Info
    std::string GetName() const { return name_; }
    ContextId GetId() const { return id_; }
    bool IsValid() const { return context_ptr_ != nullptr; }
    ScriptEnvironment* GetEnvironment() const { return env_; }
    
    // For internal use
    void* GetContextPtr() const { return context_ptr_; }
    
private:
    ScriptEnvironment* env_;
    std::string name_;
    ContextId id_;
    void* context_ptr_ = nullptr;  // v8::Global<v8::Context>*
    
    static std::atomic<ContextId> next_id_;
};

using SandboxContextPtr = std::shared_ptr<SandboxContext>;

} // namespace experiments
