/**
 * environment.cpp - ScriptEnvironment Implementation
 * 
 * Contains the full implementation of ScriptEnvironment.
 * Requires Node.js and V8 headers.
 */

#include "environment.hpp"
#include "sandbox_context.hpp"
#include "../core/logger.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>
#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

namespace experiments {

//=============================================================================
// V8 Value Debug Printer Implementation
//=============================================================================

std::string V8ValueToDebugString(void* isolate_ptr, void* value_ptr) {
    if (!isolate_ptr || !value_ptr) {
        return "<null>";
    }
    
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_ptr);
    v8::Local<v8::Value>* value = static_cast<v8::Local<v8::Value>*>(value_ptr);
    
    if (value->IsEmpty()) {
        return "<empty>";
    }
    
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::ostringstream ss;
    
    // Get type using V8's TypeOf
    v8::Local<v8::String> type_str = (*value)->TypeOf(isolate);
    v8::String::Utf8Value type_utf8(isolate, type_str);
    ss << "[" << (*type_utf8 ? *type_utf8 : "unknown") << "] ";
    
    // Get value string representation
    v8::Local<v8::String> str;
    if ((*value)->IsObject() && !(*value)->IsFunction()) {
        // For objects, use JSON.stringify for better output
        v8::MaybeLocal<v8::String> json = v8::JSON::Stringify(context, *value);
        if (json.ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            ss << (*utf8 ? *utf8 : "<stringify failed>");
        } else {
            ss << "[object]";
        }
    } else if ((*value)->ToString(context).ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        ss << (*utf8 ? *utf8 : "<toString failed>");
    } else {
        ss << "<no string representation>";
    }
    
    return ss.str();
}

namespace {

// Helper class to manage V8 scopes and context entry
class V8Scope {
public:
    explicit V8Scope(ScriptEnvironment* env) {
        if (!env) return;
        auto* setup = env->GetSetup();
        if (!setup) return;
        
        isolate_ = setup->isolate();
        locker_.emplace(isolate_);
        isolate_scope_.emplace(isolate_);
        handle_scope_.emplace(isolate_);
        context_ = setup->context();
        context_scope_.emplace(context_);
        valid_ = true;
    }

    bool IsValid() const { return valid_; }
    explicit operator bool() const { return valid_; }
    v8::Isolate* GetIsolate() const { return isolate_; }
    v8::Local<v8::Context> GetContext() const { return context_; }

private:
    bool valid_ = false;
    v8::Isolate* isolate_ = nullptr;
    std::optional<v8::Locker> locker_;
    std::optional<v8::Isolate::Scope> isolate_scope_;
    std::optional<v8::HandleScope> handle_scope_;
    v8::Local<v8::Context> context_;
    std::optional<v8::Context::Scope> context_scope_;
};

} // namespace

// Internal storage for bound native functions
struct ScriptEnvironment::NativeFunctionData {
    ScriptEnvironment* env;
    NativeCallback callback;
};

// PIMPL Implementation
struct ScriptEnvironment::Impl {
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
    MemoryMetrics cached_memory_metrics_;
    mutable std::shared_mutex mutex_;
    
    std::unordered_map<std::string, DirectiveHandler> directives_;
    mutable std::mutex directive_mutex_;
    
    std::unordered_map<std::string, std::string> modules_;
    std::unordered_map<std::string, ReloadCallback> module_watchers_;
    mutable std::mutex module_mutex_;
    
    std::unordered_map<ValueId, ValueEntry> value_registry_;
    std::atomic<ValueId> next_value_id_{1};
    mutable std::mutex value_mutex_;
    
    std::list<NativeFunctionData> native_functions_;
    mutable std::mutex native_functions_mutex_;

    Impl(EnvironmentId id, node::MultiIsolatePlatform* platform,
         std::vector<std::string> args, std::vector<std::string> exec_args,
         EnvironmentConfig config, EventEmitter* events)
        : id_(id), platform_(platform), args_(std::move(args)), exec_args_(std::move(exec_args)),
          config_(std::move(config)), events_(events) {}
};

//=============================================================================
// ScriptEnvironment Implementation
//=============================================================================

ScriptEnvironment::ScriptEnvironment(
    EnvironmentId id,
    node::MultiIsolatePlatform* platform,
    std::vector<std::string> args,
    std::vector<std::string> exec_args,
    EnvironmentConfig config,
    EventEmitter* events
) : impl_(std::make_unique<Impl>(id, platform, std::move(args), std::move(exec_args), std::move(config), events)) {
    // Shared context initialization
    impl_->shared_context_ = std::make_shared<ScriptContext>();
}

ScriptEnvironment::~ScriptEnvironment() {
    Stop(false);
}

bool ScriptEnvironment::Initialize() {
    if (impl_->initialized_.load()) return true;
    
    LOG_DEBUG("Environment", "Initializing " + impl_->config_.name);
    
    std::vector<std::string> errors;
    
    impl_->setup_ = node::CommonEnvironmentSetup::Create(
        impl_->platform_,
        &errors,
        impl_->args_,
        impl_->exec_args_,
        node::EnvironmentFlags::kOwnsProcessState
    );
    
    if (!impl_->setup_) {
        for (const auto& err : errors) {
            LOG_ERROR("Environment", impl_->config_.name + " creation failed: " + err);
        }
        return false;
    }
    
    // Set memory limits if specified
    if (impl_->config_.max_heap_size_mb > 0) {
        v8::Isolate* isolate = impl_->setup_->isolate();
        v8::Locker locker(isolate);
        // Note: ResourceConstraints should be set before isolate creation
        // For existing isolate, we can use SetRAILMode or similar
    }
    
    impl_->initialized_.store(true, std::memory_order_release);
    if (impl_->events_) impl_->events_->EmitEnvironment(EnvironmentEvent::Created, impl_->id_);
    LOG_INFO("Environment", impl_->config_.name + " initialized");
    return true;
}

bool ScriptEnvironment::Start() {
    if (!impl_->initialized_.load()) {
        if (!Initialize()) return false;
    }
    
    if (impl_->running_.exchange(true, std::memory_order_acq_rel)) {
        return true; // Already running
    }
    
    impl_->stop_requested_.store(false, std::memory_order_release);
    impl_->graceful_stop_.store(true, std::memory_order_release);
    
    impl_->thread_ = std::thread(&ScriptEnvironment::ThreadMain, this);
    
    if (impl_->events_) impl_->events_->EmitEnvironment(EnvironmentEvent::Started, impl_->id_);
    return true;
}

void ScriptEnvironment::Stop(bool graceful, std::chrono::milliseconds timeout) {
    if (!impl_->running_.load(std::memory_order_acquire)) {
        if (impl_->thread_.joinable()) impl_->thread_.join();
        return;
    }
    
    LOG_INFO("Environment", impl_->config_.name + " stopping (graceful=" + (graceful ? "true" : "false") + ")");
    
    if (impl_->events_) impl_->events_->EmitEnvironment(EnvironmentEvent::Stopping, impl_->id_);
    
    impl_->graceful_stop_.store(graceful, std::memory_order_release);
    impl_->stop_requested_.store(true, std::memory_order_release);
    
    // Wake up thread if sleeping
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex_);
        impl_->queue_cv_.notify_all();
    }
    
    if (impl_->thread_.joinable()) {
        if (graceful && timeout.count() > 0) {
            // Use a flag to track if join completed
            std::atomic<bool> joined{false};
            std::thread waiter([this, &joined]() {
                if (impl_->thread_.joinable()) {
                    impl_->thread_.join();
                }
                joined.store(true, std::memory_order_release);
            });
            
            // Wait for timeout
            auto start = std::chrono::steady_clock::now();
            while (!joined.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                    LOG_WARN("Environment", impl_->config_.name + " graceful shutdown timed out");
                    // Force stop by terminating execution
                    if (impl_->setup_ && impl_->setup_->isolate()) {
                        impl_->setup_->isolate()->TerminateExecution();
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // Wait for the waiter thread to finish
            if (waiter.joinable()) {
                waiter.join();
            }
        } else {
            // Direct join without timeout
            impl_->thread_.join();
        }
    }
    
    Cleanup();
    impl_->running_.store(false, std::memory_order_release);
    
    if (impl_->events_) impl_->events_->EmitEnvironment(EnvironmentEvent::Stopped, impl_->id_);
}

bool ScriptEnvironment::Execute(const ScriptPtr& script) {
    if (!impl_->running_.load() || !script) return false;
    
    {
        std::lock_guard lock(impl_->queue_mutex_);
        impl_->script_queue_.push(script);
    }
    // Don't notify immediately - let scripts batch up in the priority queue
    // The environment thread will pick them up on its 10ms timer cycle
    // This allows priority ordering to work correctly when multiple scripts
    // are queued in quick succession
    
    if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Queued, script->GetName());
    return true;
}

ScriptResult ScriptEnvironment::ExecuteSync(const std::string& code, std::chrono::milliseconds timeout) {
    // For sync execution, pass timeout of 0 to script (will use env default)
    // and we handle the wait timeout ourselves
    auto script = std::make_shared<Script>(code, Script::Options{});
    
    if (!Execute(script)) {
        return ScriptResult::Err(ErrorCode::NotInitialized, "Environment not running");
    }
    
    // Warn if no timeout specified - this can block indefinitely
    if (timeout == std::chrono::milliseconds::max() || timeout.count() <= 0) {
        LOG_WARN("ExecuteSync", "Called without timeout - may block indefinitely. "
                 "Consider using Execute() with async callbacks or specify a timeout.");
    }
    
    // Calculate wait timeout - handle max() specially to avoid overflow
    std::chrono::milliseconds wait_timeout;
    if (timeout == std::chrono::milliseconds::max() || timeout.count() <= 0) {
        // No timeout specified - wait indefinitely
        wait_timeout = std::chrono::milliseconds::max();
    } else {
        // Add buffer to wait timeout (but avoid overflow)
        auto buffer = std::chrono::seconds(1);
        if (timeout < std::chrono::milliseconds::max() - buffer) {
            wait_timeout = timeout + buffer;
        } else {
            wait_timeout = std::chrono::milliseconds::max();
        }
    }
    
    if (!script->Wait(wait_timeout)) {
        return ScriptResult::Err(ErrorCode::Timeout, "Wait timed out");
    }
    
    if (script->GetState() == ScriptState::Completed) {
        // Return the ScriptValue from the completed script
        return ScriptResult::Ok(std::move(script->GetResultValue()));
    }
    return ScriptResult::Err(script->GetError());
}

ScriptResult ScriptEnvironment::ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
    if (!impl_->config_.allow_file_access) {
        return ScriptResult::Err(ErrorCode::InvalidArgument, "File access not allowed");
    }
    
    if (!std::filesystem::exists(path)) {
        return ScriptResult::Err(ErrorCode::FileNotFound, "File not found: " + path.string());
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return ScriptResult::Err(ErrorCode::FileReadError, "Cannot open file: " + path.string());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return ExecuteSync(buffer.str(), timeout);
}

std::optional<double> ScriptEnvironment::ExecuteSyncNumber(const std::string& code, std::chrono::milliseconds timeout) {
    auto result = ExecuteSync(code, timeout);
    return result.ToNumber();
}

std::optional<std::string> ScriptEnvironment::ExecuteSyncString(const std::string& code, std::chrono::milliseconds timeout) {
    auto result = ExecuteSync(code, timeout);
    if (result.IsOk()) {
        return result.ToString();
    }
    return std::nullopt;
}

std::optional<bool> ScriptEnvironment::ExecuteSyncBool(const std::string& code, std::chrono::milliseconds timeout) {
    auto result = ExecuteSync(code, timeout);
    return result.ToBool();
}

std::future<ScriptResult> ScriptEnvironment::ExecuteAsync(const std::string& code, std::chrono::milliseconds timeout) {
    return std::async(std::launch::async, [this, code, timeout]() {
        return ExecuteSync(code, timeout);
    });
}

ScriptResult ScriptEnvironment::ExecuteSyncAwait(const std::string& code, std::chrono::milliseconds timeout) {
    // Wrap the code in an async IIFE and await it
    std::string wrapped = "(async () => { return (" + code + "); })()";
    
    auto result = ExecuteSync(wrapped, timeout);
    if (!result.IsOk()) {
        return result;
    }
    
    // Check if result is a Promise and wait for it
    auto& value = result.Value();
    if (!value.HasValue()) {
        return result;
    }
    
    // Check if it's a Promise by checking for 'then' method
    auto thenMethod = value.Get("then");
    if (!thenMethod.HasValue() || !thenMethod.IsFunction()) {
        // Not a Promise, return as-is
        return result;
    }
    
    // It's a Promise-like object - we need to resolve it
    // Use V8's microtask queue to resolve promises
    // It's a Promise-like object - we need to resolve it
    // Use V8's microtask queue to resolve promises
    if (!impl_->setup_) {
        return ScriptResult::Err(ErrorCode::InternalError, "Environment not initialized");
    }
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = impl_->setup_->context();
    v8::Context::Scope context_scope(context);
    
    // Get the promise value
    v8::Local<v8::Value> promise_val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto it = impl_->value_registry_.find(value.GetValueId());
        if (it == impl_->value_registry_.end()) {
            return ScriptResult::Err(ErrorCode::InternalError, "Promise value not found");
        }
        
        auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
        promise_val = global->Get(isolate);
    }
    
    if (!promise_val->IsPromise()) {
        return result;  // Not actually a Promise
    }
    
    v8::Local<v8::Promise> promise = promise_val.As<v8::Promise>();
    
    // Pump the microtask queue until promise settles
    auto start = std::chrono::steady_clock::now();
    while (promise->State() == v8::Promise::kPending) {
        isolate->PerformMicrotaskCheckpoint();
        
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) >= timeout) {
            return ScriptResult::Err(ErrorCode::Timeout, "Promise resolution timed out");
        }
        
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    if (promise->State() == v8::Promise::kRejected) {
        v8::Local<v8::Value> rejection = promise->Result();
        v8::String::Utf8Value utf8(isolate, rejection);
        std::string msg = *utf8 ? *utf8 : "Promise rejected";
        return ScriptResult::Err(ErrorCode::ExecutionError, msg);
    }
    
    // Promise fulfilled - return the resolved value
    v8::Local<v8::Value> resolved = promise->Result();
    ValueId resolved_id = RegisterValue(&resolved);
    
    std::string result_str;
    {
        v8::Local<v8::String> str;
        if (resolved->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8) result_str = *utf8;
        }
    }
    
    ScriptValue resolved_value(this, resolved_id);
    resolved_value.SetStringResult(result_str);
    
    return ScriptResult::Ok(std::move(resolved_value));
}

CompiledScriptPtr ScriptEnvironment::Compile(const std::string& code, const std::string& name) {
    if (!impl_->setup_ || !impl_->initialized_.load()) {
        return nullptr;
    }
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = impl_->setup_->context();
    v8::Context::Scope context_scope(context);
    
    v8::TryCatch try_catch(isolate);
    
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(isolate, code.c_str()).ToLocal(&source)) {
        return nullptr;
    }
    
    v8::Local<v8::Script> compiled;
    if (!v8::Script::Compile(context, source).ToLocal(&compiled)) {
        return nullptr;
    }
    
    // Store the compiled script in a Global
    auto* script_global = new v8::Global<v8::Script>(isolate, compiled);
    
    std::string script_name = name.empty() ? ("compiled_" + std::to_string(reinterpret_cast<uintptr_t>(script_global))) : name;
    auto result = std::make_shared<CompiledScript>(this, script_name);
    result->SetScriptPtr(script_global);
    result->SetSource(code);  // Store source for cache validation
    
    return result;
}

ScriptResult ScriptEnvironment::RunCompiledScript(CompiledScriptPtr script, std::chrono::milliseconds timeout) {
    if (!script || !script->IsValid() || !impl_->setup_ || !impl_->initialized_.load()) {
        return ScriptResult::Err(ErrorCode::InvalidArgument, "Invalid compiled script");
    }
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = impl_->setup_->context();
    v8::Context::Scope context_scope(context);
    
    v8::TryCatch try_catch(isolate);
    
    auto* script_global = static_cast<v8::Global<v8::Script>*>(script->GetScriptPtr());
    v8::Local<v8::Script> compiled = script_global->Get(isolate);
    
    v8::Local<v8::Value> result_val;
    if (!compiled->Run(context).ToLocal(&result_val)) {
        ScriptError error{ErrorCode::ExecutionError, "Execution error"};
        if (try_catch.HasCaught()) {
            v8::Local<v8::Message> msg = try_catch.Message();
            if (!msg.IsEmpty()) {
                v8::String::Utf8Value msg_str(isolate, msg->Get());
                error.message = *msg_str ? *msg_str : "Unknown error";
            }
        }
        return ScriptResult::Err(error.code, error.message);
    }
    
    // Convert result to ScriptValue
    std::string result_str;
    {
        v8::Local<v8::String> str;
        if (result_val->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8) result_str = *utf8;
        }
    }
    
    ValueId result_id = RegisterValue(&result_val);
    ScriptValue result_value(this, result_id);
    result_value.SetStringResult(result_str);
    
    return ScriptResult::Ok(std::move(result_value));
}

CompiledScriptPtr ScriptEnvironment::CompileFromCache(const std::vector<uint8_t>& cached_data, 
                                                       const std::string& source, 
                                                       const std::string& name) {
    return CompiledScript::FromCachedData(this, cached_data, source, name);
}

ScriptResult ScriptEnvironment::CompileFunction(const std::string& code, 
                                                 const std::vector<std::string>& param_names,
                                                 const std::vector<ScriptValue>& args) {
    if (!impl_->setup_ || !impl_->initialized_.load()) {
        return ScriptResult::Err(ErrorCode::NotInitialized, "Environment not initialized");
    }
    
    if (param_names.size() != args.size()) {
        return ScriptResult::Err(ErrorCode::InvalidArgument, "Parameter count mismatch");
    }
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = impl_->setup_->context();
    v8::Context::Scope context_scope(context);
    
    v8::TryCatch try_catch(isolate);
    
    // Create source
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(isolate, code.c_str()).ToLocal(&source)) {
        return ScriptResult::Err(ErrorCode::InternalError, "Failed to create source");
    }
    
    v8::ScriptCompiler::Source script_source(source);
    
    // Create parameter names array
    std::vector<v8::Local<v8::String>> params;
    params.reserve(param_names.size());
    for (const auto& name : param_names) {
        v8::Local<v8::String> param;
        if (!v8::String::NewFromUtf8(isolate, name.c_str()).ToLocal(&param)) {
            return ScriptResult::Err(ErrorCode::InternalError, "Failed to create parameter name");
        }
        params.push_back(param);
    }
    
    // Compile as function
    v8::Local<v8::Function> fn;
    if (!v8::ScriptCompiler::CompileFunction(
            context,
            &script_source,
            static_cast<int>(params.size()),
            params.empty() ? nullptr : params.data()).ToLocal(&fn)) {
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
    
    // Prepare arguments
    std::vector<v8::Local<v8::Value>> v8_args;
    v8_args.reserve(args.size());
    
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        for (const auto& arg : args) {
            if (!arg.HasValue()) {
                v8_args.push_back(v8::Undefined(isolate));
                continue;
            }
            auto it = impl_->value_registry_.find(arg.GetValueId());
            if (it == impl_->value_registry_.end()) {
                v8_args.push_back(v8::Undefined(isolate));
                continue;
            }
            auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
            v8_args.push_back(global->Get(isolate));
        }
    }
    
    // Call the function
    v8::Local<v8::Value> result_val;
    if (!fn->Call(context, context->Global(), 
                  static_cast<int>(v8_args.size()),
                  v8_args.empty() ? nullptr : v8_args.data()).ToLocal(&result_val)) {
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
    
    ValueId result_id = RegisterValue(&result_val);
    ScriptValue result_value(this, result_id);
    result_value.SetStringResult(result_str);
    
    return ScriptResult::Ok(std::move(result_value));
}

//=============================================================================
// Sandboxing
//=============================================================================

void ScriptEnvironment::SetIsolationLevel(IsolationLevel level) {
    impl_->isolation_level_.store(level, std::memory_order_release);
}

bool ScriptEnvironment::CanAccess(const std::string& capability) const {
    auto level = impl_->isolation_level_.load(std::memory_order_acquire);
    
    // Full access allows everything
    if (level == IsolationLevel::Full) {
        return true;
    }
    
    // Minimal only allows basic compute
    if (level == IsolationLevel::Minimal) {
        // Minimal allows: basic math, string ops, JSON
        static const std::unordered_set<std::string> minimal_allowed = {
            "math", "string", "json", "array", "object"
        };
        return minimal_allowed.count(capability) > 0;
    }
    
    // Restricted blocks fs, network, process
    if (level == IsolationLevel::Restricted) {
        static const std::unordered_set<std::string> restricted_blocked = {
            "fs", "net", "http", "https", "child_process", "process", "os"
        };
        return restricted_blocked.count(capability) == 0;
    }
    
    return false;
}

//=============================================================================
// Module Support
//=============================================================================

void ScriptEnvironment::RegisterModule(const std::string& name, const std::string& code) {
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    impl_->modules_[name] = code;
}

void ScriptEnvironment::RegisterModule(const std::string& name, const char* code) {
    RegisterModule(name, std::string(code));
}

void ScriptEnvironment::RegisterModule(const std::string& name, const std::filesystem::path& file) {
    std::ifstream ifs(file);
    if (!ifs) return;
    
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    impl_->modules_[name] = buffer.str();
}

void ScriptEnvironment::UnregisterModule(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    impl_->modules_.erase(name);
}

std::string ScriptEnvironment::GetModule(const std::string& name) const {
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    auto it = impl_->modules_.find(name);
    return it != impl_->modules_.end() ? it->second : "";
}

ScriptResult ScriptEnvironment::RequireModule(const std::string& name) {
    std::string code;
    {
        std::lock_guard<std::mutex> lock(impl_->module_mutex_);
        auto it = impl_->modules_.find(name);
        if (it == impl_->modules_.end()) {
            return ScriptResult::Err(ErrorCode::ModuleNotFound, "Module not found: " + name);
        }
        code = it->second;
    }
    
    // Wrap as CommonJS module and execute
    std::string wrapped = R"(
        (function() {
            var module = { exports: {} };
            var exports = module.exports;
            )" + code + R"(
            return module.exports;
        })()
    )";
    
    return ExecuteSync(wrapped);
}

void ScriptEnvironment::WatchModule(const std::string& name, ReloadCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    impl_->module_watchers_[name] = std::move(callback);
}

void ScriptEnvironment::UnwatchModule(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->module_mutex_);
    impl_->module_watchers_.erase(name);
}

void ScriptEnvironment::ReloadModule(const std::string& name) {
    ReloadCallback callback;
    {
        std::lock_guard<std::mutex> lock(impl_->module_mutex_);
        auto it = impl_->module_watchers_.find(name);
        if (it == impl_->module_watchers_.end()) return;
        callback = it->second;
    }
    
    // Call outside lock to prevent deadlocks
    if (callback) {
        callback(name);
    }
}

//=============================================================================
// Directive Hooks
//=============================================================================

void ScriptEnvironment::RegisterDirective(const std::string& name, DirectiveHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->directive_mutex_);
    impl_->directives_[name] = std::move(handler);
}

void ScriptEnvironment::UnregisterDirective(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->directive_mutex_);
    impl_->directives_.erase(name);
}

std::vector<std::string> ScriptEnvironment::ParseDirectives(const std::string& code) {
    std::vector<std::string> found;
    
    // Match patterns like "use X"; or 'use X'; at start of lines
    // Simple parsing - look for "use " followed by identifier and ";
    size_t pos = 0;
    while (pos < code.size()) {
        // Skip whitespace
        while (pos < code.size() && (code[pos] == ' ' || code[pos] == '\t' || code[pos] == '\n' || code[pos] == '\r')) {
            pos++;
        }
        
        // Check for "use or 'use
        if (pos + 5 < code.size()) {
            char quote = code[pos];
            if ((quote == '"' || quote == '\'') && code.substr(pos + 1, 4) == "use ") {
                pos += 5;  // Skip quote + "use "
                
                // Find directive name (until quote)
                size_t name_start = pos;
                while (pos < code.size() && code[pos] != quote) {
                    pos++;
                }
                
                if (pos < code.size() && code[pos] == quote) {
                    std::string directive = code.substr(name_start, pos - name_start);
                    // Trim trailing whitespace from directive name
                    while (!directive.empty() && directive.back() == ' ') {
                        directive.pop_back();
                    }
                    if (!directive.empty()) {
                        found.push_back(directive);
                    }
                    pos++;  // Skip closing quote
                    
                    // Skip to semicolon
                    while (pos < code.size() && code[pos] != ';') {
                        pos++;
                    }
                    if (pos < code.size()) pos++;  // Skip semicolon
                    continue;
                }
            }
        }
        
        // Not a directive, stop parsing directives
        break;
    }
    
    return found;
}

void ScriptEnvironment::ProcessDirectives(const std::string& code) {
    auto directives_found = ParseDirectives(code);
    
    std::lock_guard<std::mutex> lock(impl_->directive_mutex_);
    for (const auto& name : directives_found) {
        auto it = impl_->directives_.find(name);
        if (it != impl_->directives_.end()) {
            it->second(this, name);
        }
    }
}

EnvironmentMetrics ScriptEnvironment::GetMetrics() {
    EnvironmentMetrics metrics;
    metrics.memory = GetMemoryMetrics();
    
    {
        std::shared_lock lock(impl_->mutex_);
        metrics.execution = impl_->exec_metrics_;
    }
    
    {
        std::lock_guard lock(impl_->queue_mutex_);
        metrics.queue_size = impl_->script_queue_.size();
    }
    
    metrics.is_running = impl_->running_.load();
    return metrics;
}

MemoryMetrics ScriptEnvironment::GetMemoryMetrics() {
    // Return cached metrics - updated periodically by the environment thread
    // Cannot access isolate from another thread without deadlock
    std::shared_lock lock(impl_->mutex_);
    return impl_->cached_memory_metrics_;
}

MemoryMetrics ScriptEnvironment::CollectMemoryMetrics() {
    MemoryMetrics metrics;
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    
    v8::HeapStatistics stats;
    isolate->GetHeapStatistics(&stats);
    
    metrics.heap_size_limit = stats.heap_size_limit();
    metrics.total_heap_size = stats.total_heap_size();
    metrics.used_heap_size = stats.used_heap_size();
    metrics.external_memory = stats.external_memory();
    
    return metrics;
}

void ScriptEnvironment::ThreadMain() {
    v8::Isolate* isolate = impl_->setup_->isolate();
    node::Environment* env = impl_->setup_->env();
    uv_loop_t* loop = impl_->setup_->event_loop();
    
    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Context::Scope context_scope(impl_->setup_->context());
        
        // Bootstrap
        std::string bootstrap = 
            "const publicRequire = require('module').createRequire(process.cwd() + '/');"
            "globalThis.require = publicRequire;";
        
        // Add custom bootstrap
        if (!impl_->config_.bootstrap_script.empty()) {
            bootstrap += impl_->config_.bootstrap_script;
        }
        
        // Add module paths
        if (!impl_->config_.module_paths.empty()) {
            bootstrap += "module.paths = [";
            for (size_t i = 0; i < impl_->config_.module_paths.size(); ++i) {
                if (i > 0) bootstrap += ",";
                bootstrap += "'" + impl_->config_.module_paths[i] + "'";
            }
            bootstrap += ", ...module.paths];";
        }
        
        node::LoadEnvironment(env, bootstrap);
        
        // Initialize memory metrics cache immediately
        {
            v8::HeapStatistics stats;
            isolate->GetHeapStatistics(&stats);
            std::unique_lock lock(impl_->mutex_);
            impl_->cached_memory_metrics_.heap_size_limit = stats.heap_size_limit();
            impl_->cached_memory_metrics_.total_heap_size = stats.total_heap_size();
            impl_->cached_memory_metrics_.used_heap_size = stats.used_heap_size();
            impl_->cached_memory_metrics_.external_memory = stats.external_memory();
        }
        
        LOG_INFO("Environment", impl_->config_.name + " thread started");
        
        // Main loop
        int loop_count = 0;
        while (!impl_->stop_requested_.load(std::memory_order_acquire)) {
            ProcessScriptQueue();
            
            uv_run(loop, UV_RUN_NOWAIT);
            impl_->platform_->DrainTasks(isolate);
            isolate->PerformMicrotaskCheckpoint();
            
            // Update cached memory metrics periodically (every ~100ms)
            if (++loop_count >= 10) {
                loop_count = 0;
                v8::HeapStatistics stats;
                isolate->GetHeapStatistics(&stats);
                {
                    std::unique_lock lock(impl_->mutex_);
                    impl_->cached_memory_metrics_.heap_size_limit = stats.heap_size_limit();
                    impl_->cached_memory_metrics_.total_heap_size = stats.total_heap_size();
                    impl_->cached_memory_metrics_.used_heap_size = stats.used_heap_size();
                    impl_->cached_memory_metrics_.external_memory = stats.external_memory();
                }
            }
            
            // Release the isolate lock while waiting for work.
            // This allows other threads (e.g. ScriptResult::IsNumber) to access the isolate.
            {
                v8::Unlocker unlocker(isolate);  // Releases the Locker temporarily
                std::unique_lock lock(impl_->queue_mutex_);
                impl_->queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return !impl_->script_queue_.empty() || impl_->stop_requested_.load();
                });
            }  // Re-acquires the Locker when Unlocker goes out of scope
        }
        
        // Graceful: process remaining scripts
        if (impl_->graceful_stop_.load()) {
            LOG_DEBUG("Environment", impl_->config_.name + " processing remaining scripts");
            ProcessScriptQueue();
        } else {
            // Cancel remaining scripts
            std::lock_guard lock(impl_->queue_mutex_);
            while (!impl_->script_queue_.empty()) {
                auto script = impl_->script_queue_.top();
                impl_->script_queue_.pop();
                script->Fail(ScriptError::Make(ErrorCode::Cancelled, "Environment shut down"));
            }
        }
        
        LOG_INFO("Environment", impl_->config_.name + " thread stopping");
    }
}

void ScriptEnvironment::ProcessScriptQueue() {
    std::vector<ScriptPtr> scripts_to_run;
    
    {
        std::lock_guard lock(impl_->queue_mutex_);
        while (!impl_->script_queue_.empty()) {
            scripts_to_run.push_back(impl_->script_queue_.top());
            impl_->script_queue_.pop();
        }
    }
    
    for (const auto& script : scripts_to_run) {
        if (impl_->stop_requested_.load() && !impl_->graceful_stop_.load()) {
            script->Fail(ScriptError::Make(ErrorCode::Cancelled, "Environment shut down"));
            continue;
        }
        RunScript(script);
    }
}

void ScriptEnvironment::RunScript(const ScriptPtr& script) {
    if (!script) return;
    
    if (script->IsCancelRequested()) {
        script->Fail(ScriptError::Make(ErrorCode::Cancelled, "Script cancelled"));
        if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Cancelled, script->GetName());
        return;
    }
    
    script->Start();
    if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Started, script->GetName());
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(impl_->setup_->context());
    v8::TryCatch try_catch(isolate);
    
    // Setup timeout if specified
    auto timeout = script->GetTimeout();
    if (timeout.count() == 0) timeout = impl_->config_.default_script_timeout;
    
    std::atomic timed_out{false};
    std::atomic script_done{false};
    std::mutex timeout_mutex;
    std::condition_variable timeout_cv;
    std::thread timeout_thread;
    
    if (timeout.count() > 0) {
        timeout_thread = std::thread([&]() {
            std::unique_lock lock(timeout_mutex);
            // Wait for timeout OR script completion
            bool completed = timeout_cv.wait_for(lock, timeout, [&] {
                return script_done.load(std::memory_order_acquire);
            });
            // If timed out (not completed early) and script still running
            if (!completed && !script->IsComplete()) {
                // Set timed_out FIRST, before terminating
                timed_out.store(true, std::memory_order_release);
                // Memory barrier to ensure timed_out is visible
                std::atomic_thread_fence(std::memory_order_seq_cst);
                isolate->TerminateExecution();
            }
        });
    }
    
    // Helper to signal timeout thread to exit
    auto signalTimeoutDone = [&]() {
        if (timeout_thread.joinable()) {
            {
                std::lock_guard lock(timeout_mutex);
                script_done.store(true, std::memory_order_release);
            }
            timeout_cv.notify_one();
            timeout_thread.join();
        }
    };
    
    // Process directives (e.g., "use obs";) before compilation
    ProcessDirectives(script->GetCode());
    
    // Compile
    v8::Local<v8::String> source; // Script Source as v8::String
    if (!v8::String::NewFromUtf8(isolate, script->GetCode().c_str()).ToLocal(&source)) {
        script->Fail(ScriptError::Make(ErrorCode::InternalError, "Failed to create source"));
        signalTimeoutDone();
        return;
    }
    
    v8::Local<v8::Script> compiled; // Compile it as v8::Script
    if (!v8::Script::Compile(impl_->setup_->context(), source).ToLocal(&compiled)) {
        ScriptError error{ErrorCode::CompileError, "Compile error"};
        if (try_catch.HasCaught()) {
            v8::String::Utf8Value msg(isolate, try_catch.Exception());
            error.message = *msg ? *msg : "Compile error";
            
            v8::Local<v8::Message> message = try_catch.Message();
            if (!message.IsEmpty()) {
                error.line = message->GetLineNumber(impl_->setup_->context()).FromMaybe(0);
                error.column = message->GetStartColumn();
            }
        }
        script->Fail(error);
        signalTimeoutDone();
        return;
    }
    
    // Run
    v8::Local<v8::Value> result; // Return Value of the script as v8::Value
    bool success = compiled->Run(impl_->setup_->context()).ToLocal(&result);
    
    // Check if we timed out BEFORE signaling the timeout thread
    // (TerminateExecution causes Run to return, timed_out should already be set)
    bool was_timed_out = timed_out.load(std::memory_order_acquire);
    
    // Now signal timeout thread that we're done (if it's still waiting)
    signalTimeoutDone();
    
    if (was_timed_out && !success) {
        {
            std::unique_lock lock(impl_->mutex_);
            impl_->exec_metrics_.scripts_timed_out++;
        }
        script->Fail(ScriptError::Make(ErrorCode::Timeout, "Script execution timed out"));
        if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Timeout, script->GetName());
        isolate->CancelTerminateExecution();
        return;
    }
    
    if (!success) {
        ScriptError error{ErrorCode::RuntimeError, "Runtime error"};
        if (try_catch.HasCaught()) {
            v8::String::Utf8Value msg(isolate, try_catch.Exception());
            error.message = *msg ? *msg : "Runtime error";
            
            v8::Local<v8::Value> stack_trace;
            if (try_catch.StackTrace(impl_->setup_->context()).ToLocal(&stack_trace)) {
                v8::String::Utf8Value stack(isolate, stack_trace);
                if (*stack) error.stack = *stack;
            }
        }
        {
            std::unique_lock lock(impl_->mutex_);
            impl_->exec_metrics_.scripts_failed++;
        }
        script->Fail(error);
        if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Failed, script->GetName());
        return;
    }
    
    // Success
    std::string result_str;
    if (!result.IsEmpty() && !result->IsUndefined()) {
        v8::String::Utf8Value utf8(isolate, result);
        if (*utf8) result_str = *utf8;
    }
    
    // Register the value and create ScriptResult with env + id
    ValueId result_id = RegisterValue(&result);
    ScriptValue result_value(this, result_id);
    result_value.SetStringResult(result_str);
    script->SetResultValue(std::move(result_value));
    
    {
        std::unique_lock lock(impl_->mutex_);
        impl_->exec_metrics_.scripts_executed++;
    }
    script->Complete(result_str);
    if (impl_->events_) impl_->events_->EmitScript(ScriptEvent::Completed, script->GetName());
}

void ScriptEnvironment::Cleanup() {
    if (!impl_->setup_) return;
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    node::Environment* env = impl_->setup_->env();
    uv_loop_t* loop = impl_->setup_->event_loop();
    
    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        
        node::Stop(env);
        
        while (uv_loop_alive(loop)) {
            uv_run(loop, UV_RUN_ONCE);
            impl_->platform_->DrainTasks(isolate);
        }
    }
    
    impl_->setup_.reset();
    impl_->initialized_.store(false, std::memory_order_release);
    LOG_INFO("Environment", impl_->config_.name + " destroyed");
}

//=============================================================================
// Value Registry Implementation
//=============================================================================

ScriptEnvironment::ValueId ScriptEnvironment::RegisterValue(void* v8_local_ptr) {
    if (!impl_->setup_ || !v8_local_ptr) return INVALID_VALUE_ID;
    
    v8::Isolate* isolate = impl_->setup_->isolate();
    v8::Local<v8::Value>* local = static_cast<v8::Local<v8::Value>*>(v8_local_ptr);
    
    // Create persistent handle
    auto* global = new v8::Global<v8::Value>(isolate, *local);
    
    ValueId id = impl_->next_value_id_.fetch_add(1, std::memory_order_relaxed);
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    impl_->value_registry_[id] = { global, 1 };
    
    return id;
}

void ScriptEnvironment::ReleaseValue(ValueId id) {
    if (id == INVALID_VALUE_ID) return;
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return;
    
    if (--it->second.refcount == 0) {
        auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
        delete global;
        impl_->value_registry_.erase(it);
    }
}

void ScriptEnvironment::AddValueRef(ValueId id) {
    if (id == INVALID_VALUE_ID) return;
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it != impl_->value_registry_.end()) {
        ++it->second.refcount;
    }
}

bool ScriptEnvironment::HasValue(ValueId id) const {
    if (id == INVALID_VALUE_ID) return false;
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    return impl_->value_registry_.find(id) != impl_->value_registry_.end();
}

ScriptEnvironment::ValueType ScriptEnvironment::GetValueType(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return ValueType::Unknown;
    
    V8Scope scope(this);
    if (!scope) return ValueType::Unknown;
    v8::Isolate* isolate = scope.GetIsolate();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return ValueType::Unknown;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    if (val->IsUndefined()) return ValueType::Undefined;
    if (val->IsNull()) return ValueType::Null;
    if (val->IsBoolean()) return ValueType::Boolean;
    if (val->IsNumber()) return ValueType::Number;
    if (val->IsString()) return ValueType::String;
    if (val->IsFunction()) return ValueType::Function;
    if (val->IsArray()) return ValueType::Array;
    if (val->IsObject()) return ValueType::Object;
    return ValueType::Unknown;
}

std::string ScriptEnvironment::ValueToString(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return "";
    
    V8Scope scope(this);
    if (!scope) return "";
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return "";
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    v8::Local<v8::String> str;
    if (!val->ToString(context).ToLocal(&str)) return "";
    
    v8::String::Utf8Value utf8(isolate, str);
    return *utf8 ? *utf8 : "";
}

std::optional<double> ScriptEnvironment::ValueToNumber(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return std::nullopt;
    
    V8Scope scope(this);
    if (!scope) return std::nullopt;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return std::nullopt;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    if (!val->IsNumber()) return std::nullopt;
    return val->NumberValue(context).FromMaybe(0.0);
}

std::optional<bool> ScriptEnvironment::ValueToBool(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return std::nullopt;
    
    V8Scope scope(this);
    if (!scope) return std::nullopt;
    v8::Isolate* isolate = scope.GetIsolate();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return std::nullopt;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    return val->BooleanValue(isolate);
}

std::optional<int64_t> ScriptEnvironment::ValueToInt64(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return std::nullopt;
    
    V8Scope scope(this);
    if (!scope) return std::nullopt;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return std::nullopt;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    if (!val->IsNumber()) return std::nullopt;
    return val->IntegerValue(context).FromMaybe(0);
}

std::string ScriptEnvironment::ValueToJson(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return "null";
    
    V8Scope scope(this);
    if (!scope) return "null";
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return "null";
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    // Use JSON.stringify for proper serialization
    v8::Local<v8::String> json_str;
    if (!v8::JSON::Stringify(context, val).ToLocal(&json_str)) {
        return "null";
    }
    
    v8::String::Utf8Value utf8(isolate, json_str);
    return *utf8 ? *utf8 : "null";
}

ScriptEnvironment::ValueId ScriptEnvironment::GetProperty(ValueId obj_id, const std::string& key) {
    if (!HasValue(obj_id) || !impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    v8::Local<v8::Value> obj_val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto it = impl_->value_registry_.find(obj_id);
        if (it == impl_->value_registry_.end()) return INVALID_VALUE_ID;
        auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
        obj_val = global->Get(isolate);
    }
    
    if (!obj_val->IsObject()) return INVALID_VALUE_ID;
    v8::Local<v8::Object> obj = obj_val.As<v8::Object>();
    
    v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
    v8::MaybeLocal<v8::Value> maybe = obj->Get(context, v8_key);
    
    v8::Local<v8::Value> result;
    if (!maybe.ToLocal(&result)) return INVALID_VALUE_ID;
    
    return RegisterValue(&result);
}

ScriptEnvironment::ValueId ScriptEnvironment::GetArrayElement(ValueId arr_id, uint32_t index) {
    if (!HasValue(arr_id) || !impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    v8::Local<v8::Value> arr_val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto it = impl_->value_registry_.find(arr_id);
        if (it == impl_->value_registry_.end()) return INVALID_VALUE_ID;
        auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
        arr_val = global->Get(isolate);
    }
    
    if (!arr_val->IsArray()) return INVALID_VALUE_ID;
    v8::Local<v8::Array> arr = arr_val.As<v8::Array>();
    
    v8::MaybeLocal<v8::Value> maybe = arr->Get(context, index);
    v8::Local<v8::Value> result;
    if (!maybe.ToLocal(&result)) return INVALID_VALUE_ID;
    
    return RegisterValue(&result);
}

bool ScriptEnvironment::SetProperty(ValueId obj_id, const std::string& key, ValueId value_id) {
    if (!HasValue(obj_id) || !HasValue(value_id) || !impl_->setup_) return false;
    
    V8Scope scope(this);
    if (!scope) return false;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    v8::Local<v8::Value> obj_val, val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto obj_it = impl_->value_registry_.find(obj_id);
        auto val_it = impl_->value_registry_.find(value_id);
        if (obj_it == impl_->value_registry_.end() || val_it == impl_->value_registry_.end()) return false;
        
        obj_val = static_cast<v8::Global<v8::Value>*>(obj_it->second.global_ptr)->Get(isolate);
        val = static_cast<v8::Global<v8::Value>*>(val_it->second.global_ptr)->Get(isolate);
    }
    
    if (!obj_val->IsObject()) return false;
    v8::Local<v8::Object> obj = obj_val.As<v8::Object>();
    v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
    
    return obj->Set(context, v8_key, val).FromMaybe(false);
}

std::optional<uint32_t> ScriptEnvironment::GetLength(ValueId id) {
    if (!HasValue(id) || !impl_->setup_) return std::nullopt;
    
    V8Scope scope(this);
    if (!scope) return std::nullopt;
    v8::Isolate* isolate = scope.GetIsolate();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return std::nullopt;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    if (val->IsArray()) return val.As<v8::Array>()->Length();
    if (val->IsString()) return val.As<v8::String>()->Length();
    return std::nullopt;
}

ScriptEnvironment::ValueId ScriptEnvironment::InvokeFunction(ValueId func_id, const std::vector<ValueId>& args) {
    if (!HasValue(func_id) || !impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    v8::Local<v8::Value> func_val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto it = impl_->value_registry_.find(func_id);
        if (it == impl_->value_registry_.end()) return INVALID_VALUE_ID;
        func_val = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr)->Get(isolate);
    }
    
    if (!func_val->IsFunction()) return INVALID_VALUE_ID;
    v8::Local<v8::Function> func = func_val.As<v8::Function>();
    
    std::vector<v8::Local<v8::Value>> v8_args;
    v8_args.reserve(args.size());
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        for (ValueId arg_id : args) {
            auto it = impl_->value_registry_.find(arg_id);
            if (it != impl_->value_registry_.end()) {
                v8_args.push_back(static_cast<v8::Global<v8::Value>*>(it->second.global_ptr)->Get(isolate));
            } else {
                v8_args.push_back(v8::Undefined(isolate));
            }
        }
    }
    
    v8::TryCatch try_catch(isolate);
    v8::MaybeLocal<v8::Value> maybe = func->Call(
        context, context->Global(),
        static_cast<int>(v8_args.size()),
        v8_args.empty() ? nullptr : v8_args.data()
    );
    
    if (try_catch.HasCaught()) return INVALID_VALUE_ID;
    
    v8::Local<v8::Value> result;
    if (!maybe.ToLocal(&result)) return INVALID_VALUE_ID;
    
    return RegisterValue(&result);
}

ScriptEnvironment::ValueId ScriptEnvironment::CallMethod(ValueId obj_id, const std::string& method, const std::vector<ValueId>& args) {
    if (!HasValue(obj_id) || !impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    // Get the object
    v8::Local<v8::Value> obj_val;
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        auto it = impl_->value_registry_.find(obj_id);
        if (it == impl_->value_registry_.end()) return INVALID_VALUE_ID;
        obj_val = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr)->Get(isolate);
    }
    
    if (!obj_val->IsObject()) return INVALID_VALUE_ID;
    v8::Local<v8::Object> obj = obj_val.As<v8::Object>();
    
    // Get the method
    v8::Local<v8::String> v8_method = v8::String::NewFromUtf8(isolate, method.c_str()).ToLocalChecked();
    v8::MaybeLocal<v8::Value> maybe_func = obj->Get(context, v8_method);
    v8::Local<v8::Value> func_val;
    if (!maybe_func.ToLocal(&func_val) || !func_val->IsFunction()) {
        return INVALID_VALUE_ID;
    }
    v8::Local<v8::Function> func = func_val.As<v8::Function>();
    
    // Build arguments
    std::vector<v8::Local<v8::Value>> v8_args;
    v8_args.reserve(args.size());
    {
        std::lock_guard<std::mutex> lock(impl_->value_mutex_);
        for (ValueId arg_id : args) {
            auto it = impl_->value_registry_.find(arg_id);
            if (it != impl_->value_registry_.end()) {
                v8_args.push_back(static_cast<v8::Global<v8::Value>*>(it->second.global_ptr)->Get(isolate));
            } else {
                v8_args.push_back(v8::Undefined(isolate));
            }
        }
    }
    
    // Call with obj as 'this'
    v8::TryCatch try_catch(isolate);
    v8::MaybeLocal<v8::Value> maybe_result = func->Call(
        context, obj,  // 'this' = the object
        static_cast<int>(v8_args.size()),
        v8_args.empty() ? nullptr : v8_args.data()
    );
    
    if (try_catch.HasCaught()) return INVALID_VALUE_ID;
    
    v8::Local<v8::Value> result;
    if (!maybe_result.ToLocal(&result)) return INVALID_VALUE_ID;
    
    return RegisterValue(&result);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateNumber(double value) {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Value> val = v8::Number::New(isolate, value);
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateString(const std::string& value) {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Value> val = v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateBool(bool value) {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Value> val = v8::Boolean::New(isolate, value);
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateUndefined() {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Value> val = v8::Undefined(isolate);
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateNull() {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Value> val = v8::Null(isolate);
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateArray(size_t length) {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Array> arr = v8::Array::New(isolate, static_cast<int>(length));
    v8::Local<v8::Value> val = arr;
    return RegisterValue(&val);
}

ScriptEnvironment::ValueId ScriptEnvironment::CreateObject() {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    v8::Local<v8::Value> val = obj;
    return RegisterValue(&val);
}

std::vector<std::string> ScriptEnvironment::GetObjectKeys(ValueId obj_id) {
    std::vector<std::string> result;
    if (!HasValue(obj_id) || !impl_->setup_) return result;
    
    V8Scope scope(this);
    if (!scope) return result;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(obj_id);
    if (it == impl_->value_registry_.end()) return result;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    if (!val->IsObject()) return result;
    
    v8::Local<v8::Object> obj = val.As<v8::Object>();
    v8::Local<v8::Array> keys;
    if (!obj->GetOwnPropertyNames(context).ToLocal(&keys)) return result;
    
    for (uint32_t i = 0; i < keys->Length(); i++) {
        v8::Local<v8::Value> key;
        if (keys->Get(context, i).ToLocal(&key)) {
            v8::String::Utf8Value utf8(isolate, key);
            if (*utf8) result.push_back(*utf8);
        }
    }
    
    return result;
}

void ScriptEnvironment::SetGlobal(const std::string& name, ValueId value_id) {
    if (!HasValue(value_id) || !impl_->setup_) return;
    
    V8Scope scope(this);
    if (!scope) return;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(value_id);
    if (it == impl_->value_registry_.end()) return;
    
    auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
    v8::Local<v8::Value> val = global->Get(isolate);
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    context->Global()->Set(context, key, val).Check();
}

// SetGlobal overloads removed - replaced by template in header

// SetGlobal overloads removed - replaced by template in header

void ScriptEnvironment::Bind(const std::string& name, NativeCallback callback) {
    if (!impl_->setup_) return;
    
    V8Scope scope(this);
    if (!scope) return;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    // Store callback in our registry to keep it alive
    {
        std::lock_guard<std::mutex> lock(impl_->native_functions_mutex_);
        impl_->native_functions_.push_back({this, std::move(callback)});
    }
    
    // Create External pointing to the stable address in the list
    // safe because std::list iterators/pointers are stable
    NativeFunctionData* data_ptr = &impl_->native_functions_.back();
    v8::Local<v8::External> data = v8::External::New(isolate, data_ptr);
    
    // Create FunctionTemplate with the router and data
    v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate, BindCallbackRouter, data);
    
    // Get Function and set on Global object
    v8::Local<v8::Function> func;
    if (tpl->GetFunction(context).ToLocal(&func)) {
        v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
        context->Global()->Set(context, key, func).Check();
    }
}

void ScriptEnvironment::BindCallbackRouter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    
    // Retrieve data
    v8::Local<v8::External> data = info.Data().As<v8::External>();
    auto* func_data = static_cast<NativeFunctionData*>(data->Value());
    ScriptEnvironment* env = func_data->env;
    
    // Convert arguments to ScriptValue
    std::vector<ScriptValue> args;
    args.reserve(info.Length());
    
    for (int i = 0; i < info.Length(); i++) {
        v8::Local<v8::Value> val = info[i];
        // Register value to get an ID (thread-safe)
        ValueId id = env->RegisterValue(&val);
        // Create ScriptValue (refcounting handled by RegisterValue starting at 1, 
        // passing to ScriptValue constructor which takes ownership/adds ref? 
        // Need to check ScriptValue constructor semantics.)
        
        // ScriptValue(env, id) constructor does NOT increment refcount by default? 
        // Let's check. 
        // If RegisterValue returns ID with refcount 1, and ScriptValue takes it, 
        // ScriptValue dtor will decrement. So it consumes the initial ref.
        // Wait, RegisterValue sets refcount=1. 
        // ScriptValue dtor decrements. 0 -> delete.
        // So passing ID from RegisterValue to ScriptValue is correct transfer of ownership 
        // IF ScriptValue doesn't increment on construction. 
        // Re-read ScriptValue code... 
        // Actually ScriptValue(env, id) captures the ID. It does NOT increment.
        // Copy ctor increments.
        // So this is correct: RegisterValue gives us 1 ref, ScriptValue takes it.
        args.emplace_back(env, id);
    }
    
    // Call the native callback
    ScriptValue result_val = func_data->callback(args);
    
    // Convert result back to V8
    // We need to get the underlying V8 value from result_val.
    // Since ScriptEnvironment doesn't expose "GetValue(id)", we need a friend or helper.
    // But wait, we are inside ScriptEnvironment static member!
    // We can access private members of 'env'.
    
    if (result_val.GetValueId() != INVALID_VALUE_ID) {
        std::lock_guard<std::mutex> lock(env->impl_->value_mutex_);
        auto it = env->impl_->value_registry_.find(result_val.GetValueId());
        if (it != env->impl_->value_registry_.end()) {
            auto* global = static_cast<v8::Global<v8::Value>*>(it->second.global_ptr);
            info.GetReturnValue().Set(global->Get(isolate));
        } else {
            info.GetReturnValue().SetUndefined();
        }
    } else {
       info.GetReturnValue().SetUndefined();
    }
}

ScriptEnvironment::ValueId ScriptEnvironment::GetGlobal(const std::string& name) {
    if (!impl_->setup_) return INVALID_VALUE_ID;
    
    V8Scope scope(this);
    if (!scope) return INVALID_VALUE_ID;
    v8::Isolate* isolate = scope.GetIsolate();
    v8::Local<v8::Context> context = scope.GetContext();
    
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::Value> val;
    if (!context->Global()->Get(context, key).ToLocal(&val)) {
        return INVALID_VALUE_ID;
    }
    
    return RegisterValue(&val);
}

//=============================================================================
// Sandbox Context Support
//=============================================================================

SandboxContextPtr ScriptEnvironment::CreateSandbox(const std::string& name) {
    auto sandbox = std::make_shared<SandboxContext>(this, name);
    if (!sandbox->Initialize()) {
        return nullptr;
    }
    return sandbox;
}

SandboxContextPtr ScriptEnvironment::CreateSandbox(const std::map<std::string, ScriptValue>& sandbox_values, 
                                                     const std::string& name) {
    auto sandbox = std::make_shared<SandboxContext>(this, name);
    if (!sandbox->Initialize(sandbox_values)) {
        return nullptr;
    }
    return sandbox;
}

ScriptEnvironment::ValueEntry* ScriptEnvironment::GetValueEntry(ValueId id) {
    std::lock_guard<std::mutex> lock(impl_->value_mutex_);
    auto it = impl_->value_registry_.find(id);
    if (it == impl_->value_registry_.end()) return nullptr;
    return &it->second;
}

ScriptEnvironment::EnvironmentId ScriptEnvironment::GetId() const {
    return impl_->id_;
}

std::string ScriptEnvironment::GetName() const {
    std::shared_lock lock(impl_->mutex_);
    return impl_->config_.name;
}

const EnvironmentConfig& ScriptEnvironment::GetConfig() const {
    return impl_->config_;
}

bool ScriptEnvironment::IsRunning() const {
    return impl_->running_.load(std::memory_order_acquire);
}

bool ScriptEnvironment::IsInitialized() const {
    return impl_->initialized_.load(std::memory_order_acquire);
}

ScriptEnvironment::IsolationLevel ScriptEnvironment::GetIsolationLevel() const {
    return impl_->isolation_level_.load();
}

ScriptContextPtr ScriptEnvironment::GetContext() const
{
    return impl_->shared_context_;
}

node::CommonEnvironmentSetup* ScriptEnvironment::GetSetup() const {
    return impl_->setup_.get();
}

} // namespace experiments