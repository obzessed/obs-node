#pragma once

/**
 * environment.hpp - ScriptEnvironment Class Declaration
 * 
 * A ScriptEnvironment wraps a Node.js isolate and provides script execution.
 * This header declares the class; implementation requires Node.js headers.
 */

#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <queue>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <optional>
#include <future>

#include "script_class.hpp"
#include "script_result.hpp"
#include "compiled_script.hpp"

// Forward declaration
namespace experiments {
    class SandboxContext;
    using SandboxContextPtr = std::shared_ptr<SandboxContext>;
}
#include "../core/error.hpp"
#include "../core/metrics.hpp"
#include "../core/events.hpp"
#include "../config/environment_config.hpp"
#include "../config/script_context.hpp"

// Forward declarations for Node.js types
namespace node {
    class MultiIsolatePlatform;
    class CommonEnvironmentSetup;
}

namespace experiments {

//=============================================================================
// ScriptEnvironment - Enhanced with config, metrics, graceful shutdown
//=============================================================================

class ScriptEnvironment : public std::enable_shared_from_this<ScriptEnvironment> {
public:
    using EnvironmentId = uint64_t;
    using ValueId = uint64_t;
    static constexpr ValueId INVALID_VALUE_ID = 0;
    
    // Value registry entry (public for sandbox context access)
    struct ValueEntry {
        void* global_ptr = nullptr;  // v8::Global<v8::Value>*
        uint32_t refcount = 1;
    };
    
    // Sandbox isolation levels
    enum class IsolationLevel {
        Full,       // All capabilities enabled
        Restricted, // No fs, network access
        Minimal     // Pure compute only (no timers, no I/O)
    };
    
    ScriptEnvironment(
        EnvironmentId id,
        node::MultiIsolatePlatform* platform,
        std::vector<std::string> args,
        std::vector<std::string> exec_args,
        EnvironmentConfig config,
        EventEmitter* events
    );
    
    ~ScriptEnvironment();
    
    // Non-copyable, non-movable
    ScriptEnvironment(const ScriptEnvironment&) = delete;
    ScriptEnvironment& operator=(const ScriptEnvironment&) = delete;
    
    bool Initialize();
    bool Start();
    void Stop(bool graceful = true, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    
    bool Execute(const ScriptPtr& script);
    ScriptResult ExecuteSync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    ScriptResult ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Direct primitive execution (convenience)
    std::optional<double> ExecuteSyncNumber(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    std::optional<std::string> ExecuteSyncString(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    std::optional<bool> ExecuteSyncBool(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Async execution
    std::future<ScriptResult> ExecuteAsync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Promise-aware execution (waits for Promise resolution)
    ScriptResult ExecuteSyncAwait(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Script precompilation (caching)
    CompiledScriptPtr Compile(const std::string& code, const std::string& name = "");
    CompiledScriptPtr CompileFromCache(const std::vector<uint8_t>& cached_data, 
                                        const std::string& source, 
                                        const std::string& name = "");
    ScriptResult RunCompiledScript(CompiledScriptPtr script, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // CompileFunction - compile code as function with parameters (REPL-style)
    ScriptResult CompileFunction(const std::string& code, 
                                  const std::vector<std::string>& param_names,
                                  const std::vector<ScriptValue>& args);
    
    
    // Accessors
    EnvironmentId GetId() const { return id_; }
    std::string GetName() const { std::shared_lock lock(mutex_); return config_.name; }
    const EnvironmentConfig& GetConfig() const { return config_; }
    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    bool IsInitialized() const { return initialized_.load(std::memory_order_acquire); }
    
    // Metrics
    EnvironmentMetrics GetMetrics();
    MemoryMetrics GetMemoryMetrics();
    
    // Context
    ScriptContextPtr GetContext() { return shared_context_; }
    
    // Sandboxing
    void SetIsolationLevel(IsolationLevel level);
    IsolationLevel GetIsolationLevel() const { return isolation_level_.load(); }
    bool CanAccess(const std::string& capability) const;  // Check if capability is allowed
    
    // Directive hooks (e.g., "use obs"; -> injects obs global)
    using DirectiveHandler = std::function<void(ScriptEnvironment*, const std::string& directive)>;
    void RegisterDirective(const std::string& name, DirectiveHandler handler);
    void UnregisterDirective(const std::string& name);
    
    // Module support (virtual modules that can be imported)
    void RegisterModule(const std::string& name, const std::string& code);
    void RegisterModule(const std::string& name, const char* code);  // Avoid path ambiguity
    void RegisterModule(const std::string& name, const std::filesystem::path& file);
    void UnregisterModule(const std::string& name);
    std::string GetModule(const std::string& name) const;
    ScriptResult RequireModule(const std::string& name);  // Execute and return exports
    
    // Hot module reload
    using ReloadCallback = std::function<void(const std::string& moduleName)>;
    void WatchModule(const std::string& name, ReloadCallback callback);
    void UnwatchModule(const std::string& name);
    void ReloadModule(const std::string& name);  // Trigger reload callbacks
    
    // Sandbox context (vm-style isolated contexts)
    SandboxContextPtr CreateSandbox(const std::string& name = "");
    SandboxContextPtr CreateSandbox(const std::map<std::string, ScriptValue>& sandbox, 
                                     const std::string& name = "");
    
    // Internal accessors for sandbox support
    node::CommonEnvironmentSetup* GetSetup() const { return setup_.get(); }
    ValueEntry* GetValueEntry(ValueId id);
    
private:
    void ThreadMain();
    void ProcessScriptQueue();
    void RunScript(const ScriptPtr& script);
    void Cleanup();
    MemoryMetrics CollectMemoryMetrics();
    
    EnvironmentId id_;
    node::MultiIsolatePlatform* platform_;
    std::vector<std::string> args_;
    std::vector<std::string> exec_args_;
    EnvironmentConfig config_;
    EventEmitter* events_;
    
    std::unique_ptr<node::CommonEnvironmentSetup> setup_;
    ScriptContextPtr shared_context_;
    
    std::thread thread_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> graceful_stop_{true};
    std::atomic<IsolationLevel> isolation_level_{IsolationLevel::Full};
    
    std::priority_queue<ScriptPtr, std::vector<ScriptPtr>, ScriptPriorityCompare> script_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    ExecutionMetrics exec_metrics_;
    MemoryMetrics cached_memory_metrics_;  // Updated by env thread
    mutable std::shared_mutex mutex_;
    
    // Directive hooks storage
    std::unordered_map<std::string, DirectiveHandler> directives_;
    mutable std::mutex directive_mutex_;
    std::vector<std::string> ParseDirectives(const std::string& code);
    void ProcessDirectives(const std::string& code);
    
    // Module registry
    std::unordered_map<std::string, std::string> modules_;  // name -> code
    std::unordered_map<std::string, ReloadCallback> module_watchers_;
    mutable std::mutex module_mutex_;
    
    //=========================================================================
    // Value Registry - stores persistent V8 values by ID
    //=========================================================================
public:
    // Register a value (called from env thread during script execution)
    ValueId RegisterValue(void* v8_local_value);  // Takes v8::Local<v8::Value>*
    
    // Release a value (decrements refcount, removes when 0)
    void ReleaseValue(ValueId id);
    
    // Add reference to a value (increments refcount)
    void AddValueRef(ValueId id);
    
    // Check if value exists
    bool HasValue(ValueId id) const;
    
    //=========================================================================
    // Delegated Operations - called from any thread, executed on env thread
    //=========================================================================
    
    // Value information
    enum class ValueType { Undefined, Null, Boolean, Number, String, Object, Array, Function, Unknown };
    ValueType GetValueType(ValueId id);
    
    // Extract primitives (copies out, safe from any thread)
    std::string ValueToString(ValueId id);
    std::optional<double> ValueToNumber(ValueId id);
    std::optional<bool> ValueToBool(ValueId id);
    std::optional<int64_t> ValueToInt64(ValueId id);
    std::string ValueToJson(ValueId id);  // JSON.stringify
    
    // Object/Array access
    ValueId GetProperty(ValueId obj_id, const std::string& key);
    ValueId GetArrayElement(ValueId arr_id, uint32_t index);
    bool SetProperty(ValueId obj_id, const std::string& key, ValueId value_id);
    std::optional<uint32_t> GetLength(ValueId id);
    
    // Function calling
    ValueId InvokeFunction(ValueId func_id, const std::vector<ValueId>& args);
    ValueId CallMethod(ValueId obj_id, const std::string& method, const std::vector<ValueId>& args);
    
    // Create primitive values
    ValueId CreateNumber(double value);
    ValueId CreateString(const std::string& value);
    ValueId CreateBool(bool value);
    ValueId CreateUndefined();
    ValueId CreateNull();
    ValueId CreateArray(size_t length = 0);
    ValueId CreateObject();
    
    // Object property enumeration
    std::vector<std::string> GetObjectKeys(ValueId obj_id);
    
    // Global object injection
    void SetGlobal(const std::string& name, ValueId value_id);
    void SetGlobal(const std::string& name, double value);
    void SetGlobal(const std::string& name, const std::string& value);
    void SetGlobal(const std::string& name, const char* value);  // Avoid bool ambiguity
    void SetGlobal(const std::string& name, bool value);
    ValueId GetGlobal(const std::string& name);
    
private:
    std::unordered_map<ValueId, ValueEntry> value_registry_;
    std::atomic<ValueId> next_value_id_{1};
    mutable std::mutex value_mutex_;
    
    // Helper: run operation on env thread
    template<typename F>
    auto RunOnEnvThread(F&& func) -> decltype(func());
};

using ScriptEnvironmentPtr = std::shared_ptr<ScriptEnvironment>;

} // namespace experiments

