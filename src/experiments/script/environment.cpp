/**
 * environment.cpp - ScriptEnvironment Implementation
 * 
 * Contains the full implementation of ScriptEnvironment.
 * Requires Node.js and V8 headers.
 */

#include "environment.hpp"
#include "../core/logger.hpp"

#include <fstream>
#include <sstream>
#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

namespace experiments {



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
) : id_(id)
  , platform_(platform)
  , args_(std::move(args))
  , exec_args_(std::move(exec_args))
  , config_(std::move(config))
  , events_(events)
  , shared_context_(std::make_shared<ScriptContext>()) {}

ScriptEnvironment::~ScriptEnvironment() {
    Stop(false);
}

bool ScriptEnvironment::Initialize() {
    if (initialized_.load()) return true;
    
    LOG_DEBUG("Environment", "Initializing " + config_.name);
    
    std::vector<std::string> errors;
    
    setup_ = node::CommonEnvironmentSetup::Create(
        platform_,
        &errors,
        args_,
        exec_args_,
        node::EnvironmentFlags::kOwnsProcessState
    );
    
    if (!setup_) {
        for (const auto& err : errors) {
            LOG_ERROR("Environment", config_.name + " creation failed: " + err);
        }
        return false;
    }
    
    // Set memory limits if specified
    if (config_.max_heap_size_mb > 0) {
        v8::Isolate* isolate = setup_->isolate();
        v8::Locker locker(isolate);
        // Note: ResourceConstraints should be set before isolate creation
        // For existing isolate, we can use SetRAILMode or similar
    }
    
    initialized_.store(true, std::memory_order_release);
    if (events_) events_->EmitEnvironment(EnvironmentEvent::Created, id_);
    LOG_INFO("Environment", config_.name + " initialized");
    return true;
}

bool ScriptEnvironment::Start() {
    if (!initialized_.load() || running_.load()) return false;
    
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&ScriptEnvironment::ThreadMain, this);
    
    if (events_) events_->EmitEnvironment(EnvironmentEvent::Started, id_);
    return true;
}

void ScriptEnvironment::Stop(bool graceful, std::chrono::milliseconds timeout) {
    if (!running_.load()) return;
    
    LOG_INFO("Environment", config_.name + " stopping (graceful=" + (graceful ? "true" : "false") + ")");
    
    if (events_) events_->EmitEnvironment(EnvironmentEvent::Stopping, id_);
    
    graceful_stop_.store(graceful, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    
    if (thread_.joinable()) {
        if (graceful && timeout.count() > 0) {
            // Use a flag to track if join completed
            std::atomic<bool> joined{false};
            std::thread waiter([this, &joined]() {
                if (thread_.joinable()) {
                    thread_.join();
                }
                joined.store(true, std::memory_order_release);
            });
            
            // Wait for timeout
            auto start = std::chrono::steady_clock::now();
            while (!joined.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                    LOG_WARN("Environment", config_.name + " graceful shutdown timed out");
                    // Force stop by terminating execution
                    if (setup_ && setup_->isolate()) {
                        setup_->isolate()->TerminateExecution();
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
            thread_.join();
        }
    }
    
    Cleanup();
    running_.store(false, std::memory_order_release);
    
    if (events_) events_->EmitEnvironment(EnvironmentEvent::Stopped, id_);
}

bool ScriptEnvironment::Execute(const ScriptPtr& script) {
    if (!running_.load() || !script) return false;
    
    {
        std::lock_guard lock(queue_mutex_);
        script_queue_.push(script);
    }
    // Don't notify immediately - let scripts batch up in the priority queue
    // The environment thread will pick them up on its 10ms timer cycle
    // This allows priority ordering to work correctly when multiple scripts
    // are queued in quick succession
    
    if (events_) events_->EmitScript(ScriptEvent::Queued, script->GetName());
    return true;
}

Result<std::string> ScriptEnvironment::ExecuteSync(const std::string& code, std::chrono::milliseconds timeout) {
    // For sync execution, pass timeout of 0 to script (will use env default)
    // and we handle the wait timeout ourselves
    auto script = std::make_shared<Script>(code, Script::Options{});
    
    if (!Execute(script)) {
        return ScriptError::Make(ErrorCode::NotInitialized, "Environment not running");
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
        return ScriptError::Make(ErrorCode::Timeout, "Wait timed out");
    }
    
    if (script->GetState() == ScriptState::Completed) {
        return script->GetResult();
    }
    return script->GetError();
}

Result<std::string> ScriptEnvironment::ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
    if (!config_.allow_file_access) {
        return ScriptError::Make(ErrorCode::InvalidArgument, "File access not allowed");
    }
    
    if (!std::filesystem::exists(path)) {
        return ScriptError::Make(ErrorCode::FileNotFound, "File not found: " + path.string());
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return ScriptError::Make(ErrorCode::FileReadError, "Cannot open file: " + path.string());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return ExecuteSync(buffer.str(), timeout);
}

EnvironmentMetrics ScriptEnvironment::GetMetrics() {
    EnvironmentMetrics metrics;
    metrics.memory = GetMemoryMetrics();
    
    {
        std::shared_lock lock(mutex_);
        metrics.execution = exec_metrics_;
    }
    
    {
        std::lock_guard lock(queue_mutex_);
        metrics.queue_size = script_queue_.size();
    }
    
    metrics.is_running = running_.load();
    return metrics;
}

MemoryMetrics ScriptEnvironment::GetMemoryMetrics() {
    // Return cached metrics - updated periodically by the environment thread
    // Cannot access isolate from another thread without deadlock
    std::shared_lock lock(mutex_);
    return cached_memory_metrics_;
}

MemoryMetrics ScriptEnvironment::CollectMemoryMetrics() {
    MemoryMetrics metrics;
    
    v8::Isolate* isolate = setup_->isolate();
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
    v8::Isolate* isolate = setup_->isolate();
    node::Environment* env = setup_->env();
    uv_loop_t* loop = setup_->event_loop();
    
    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Context::Scope context_scope(setup_->context());
        
        // Bootstrap
        std::string bootstrap = 
            "const publicRequire = require('module').createRequire(process.cwd() + '/');"
            "globalThis.require = publicRequire;";
        
        // Add custom bootstrap
        if (!config_.bootstrap_script.empty()) {
            bootstrap += config_.bootstrap_script;
        }
        
        // Add module paths
        if (!config_.module_paths.empty()) {
            bootstrap += "module.paths = [";
            for (size_t i = 0; i < config_.module_paths.size(); ++i) {
                if (i > 0) bootstrap += ",";
                bootstrap += "'" + config_.module_paths[i] + "'";
            }
            bootstrap += ", ...module.paths];";
        }
        
        node::LoadEnvironment(env, bootstrap);
        
        // Initialize memory metrics cache immediately
        {
            v8::HeapStatistics stats;
            isolate->GetHeapStatistics(&stats);
            std::unique_lock lock(mutex_);
            cached_memory_metrics_.heap_size_limit = stats.heap_size_limit();
            cached_memory_metrics_.total_heap_size = stats.total_heap_size();
            cached_memory_metrics_.used_heap_size = stats.used_heap_size();
            cached_memory_metrics_.external_memory = stats.external_memory();
        }
        
        LOG_INFO("Environment", config_.name + " thread started");
        
        // Main loop
        int loop_count = 0;
        while (!stop_requested_.load(std::memory_order_acquire)) {
            ProcessScriptQueue();
            
            uv_run(loop, UV_RUN_NOWAIT);
            platform_->DrainTasks(isolate);
            isolate->PerformMicrotaskCheckpoint();
            
            // Update cached memory metrics periodically (every ~100ms)
            if (++loop_count >= 10) {
                loop_count = 0;
                v8::HeapStatistics stats;
                isolate->GetHeapStatistics(&stats);
                {
                    std::unique_lock lock(mutex_);
                    cached_memory_metrics_.heap_size_limit = stats.heap_size_limit();
                    cached_memory_metrics_.total_heap_size = stats.total_heap_size();
                    cached_memory_metrics_.used_heap_size = stats.used_heap_size();
                    cached_memory_metrics_.external_memory = stats.external_memory();
                }
            }
            
            // Release the isolate lock while waiting for work.
            // This allows other threads (e.g. ScriptResult::IsNumber) to access the isolate.
            {
                v8::Unlocker unlocker(isolate);  // Releases the Locker temporarily
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return !script_queue_.empty() || stop_requested_.load();
                });
            }  // Re-acquires the Locker when Unlocker goes out of scope
        }
        
        // Graceful: process remaining scripts
        if (graceful_stop_.load()) {
            LOG_DEBUG("Environment", config_.name + " processing remaining scripts");
            ProcessScriptQueue();
        } else {
            // Cancel remaining scripts
            std::lock_guard lock(queue_mutex_);
            while (!script_queue_.empty()) {
                auto script = script_queue_.top();
                script_queue_.pop();
                script->Fail(ScriptError::Make(ErrorCode::Cancelled, "Environment shut down"));
            }
        }
        
        LOG_INFO("Environment", config_.name + " thread stopping");
    }
}

void ScriptEnvironment::ProcessScriptQueue() {
    std::vector<ScriptPtr> scripts_to_run;
    
    {
        std::lock_guard lock(queue_mutex_);
        while (!script_queue_.empty()) {
            scripts_to_run.push_back(script_queue_.top());
            script_queue_.pop();
        }
    }
    
    for (const auto& script : scripts_to_run) {
        if (stop_requested_.load() && !graceful_stop_.load()) {
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
        if (events_) events_->EmitScript(ScriptEvent::Cancelled, script->GetName());
        return;
    }
    
    script->Start();
    if (events_) events_->EmitScript(ScriptEvent::Started, script->GetName());
    
    v8::Isolate* isolate = setup_->isolate();
    v8::HandleScope handle_scope(isolate);
    v8::Context::Scope context_scope(setup_->context());
    v8::TryCatch try_catch(isolate);
    
    // Setup timeout if specified
    auto timeout = script->GetTimeout();
    if (timeout.count() == 0) timeout = config_.default_script_timeout;
    
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
    
    // Compile
    v8::Local<v8::String> source; // Script Source as v8::String
    if (!v8::String::NewFromUtf8(isolate, script->GetCode().c_str()).ToLocal(&source)) {
        script->Fail(ScriptError::Make(ErrorCode::InternalError, "Failed to create source"));
        signalTimeoutDone();
        return;
    }
    
    v8::Local<v8::Script> compiled; // Compile it as v8::Script
    if (!v8::Script::Compile(setup_->context(), source).ToLocal(&compiled)) {
        ScriptError error{ErrorCode::CompileError, "Compile error"};
        if (try_catch.HasCaught()) {
            v8::String::Utf8Value msg(isolate, try_catch.Exception());
            error.message = *msg ? *msg : "Compile error";
            
            v8::Local<v8::Message> message = try_catch.Message();
            if (!message.IsEmpty()) {
                error.line = message->GetLineNumber(setup_->context()).FromMaybe(0);
                error.column = message->GetStartColumn();
            }
        }
        script->Fail(error);
        signalTimeoutDone();
        return;
    }
    
    // Run
    v8::Local<v8::Value> result; // Return Value of the script as v8::Value
    bool success = compiled->Run(setup_->context()).ToLocal(&result);
    
    // Check if we timed out BEFORE signaling the timeout thread
    // (TerminateExecution causes Run to return, timed_out should already be set)
    bool was_timed_out = timed_out.load(std::memory_order_acquire);
    
    // Now signal timeout thread that we're done (if it's still waiting)
    signalTimeoutDone();
    
    if (was_timed_out && !success) {
        {
            std::unique_lock lock(mutex_);
            exec_metrics_.scripts_timed_out++;
        }
        script->Fail(ScriptError::Make(ErrorCode::Timeout, "Script execution timed out"));
        if (events_) events_->EmitScript(ScriptEvent::Timeout, script->GetName());
        isolate->CancelTerminateExecution();
        return;
    }
    
    if (!success) {
        ScriptError error{ErrorCode::RuntimeError, "Runtime error"};
        if (try_catch.HasCaught()) {
            v8::String::Utf8Value msg(isolate, try_catch.Exception());
            error.message = *msg ? *msg : "Runtime error";
            
            v8::Local<v8::Value> stack_trace;
            if (try_catch.StackTrace(setup_->context()).ToLocal(&stack_trace)) {
                v8::String::Utf8Value stack(isolate, stack_trace);
                if (*stack) error.stack = *stack;
            }
        }
        {
            std::unique_lock lock(mutex_);
            exec_metrics_.scripts_failed++;
        }
        script->Fail(error);
        if (events_) events_->EmitScript(ScriptEvent::Failed, script->GetName());
        return;
    }
    
    // Success
    std::string result_str;
    if (!result.IsEmpty() && !result->IsUndefined()) {
        v8::String::Utf8Value utf8(isolate, result);
        if (*utf8) result_str = *utf8;
    }
    
    // Store the raw V8 value in ScriptResult
    ScriptResult result_value = ScriptResult::Create(isolate, result);
    result_value.SetStringResult(result_str);
    script->SetResultValue(std::move(result_value));
    
    {
        std::unique_lock lock(mutex_);
        exec_metrics_.scripts_executed++;
    }
    script->Complete(result_str);
    if (events_) events_->EmitScript(ScriptEvent::Completed, script->GetName());
}

void ScriptEnvironment::Cleanup() {
    if (!setup_) return;
    
    v8::Isolate* isolate = setup_->isolate();
    node::Environment* env = setup_->env();
    uv_loop_t* loop = setup_->event_loop();
    
    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        
        node::Stop(env);
        
        while (uv_loop_alive(loop)) {
            uv_run(loop, UV_RUN_ONCE);
            platform_->DrainTasks(isolate);
        }
    }
    
    setup_.reset();
    initialized_.store(false, std::memory_order_release);
    LOG_INFO("Environment", config_.name + " destroyed");
}

} // namespace experiments
