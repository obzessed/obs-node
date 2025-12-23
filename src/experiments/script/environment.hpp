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

#include "script_class.hpp"
#include "../core/errors.hpp"
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
// ScriptEnvironment Declaration
//=============================================================================

class ScriptEnvironment : public std::enable_shared_from_this<ScriptEnvironment> {
public:
    using EnvironmentId = uint64_t;
    
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
    Result<std::string> ExecuteSync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    Result<std::string> ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
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
    
    std::priority_queue<ScriptPtr, std::vector<ScriptPtr>, ScriptPriorityCompare> script_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    ExecutionMetrics exec_metrics_;
    MemoryMetrics cached_memory_metrics_;  // Updated by env thread
    mutable std::shared_mutex mutex_;
};

using ScriptEnvironmentPtr = std::shared_ptr<ScriptEnvironment>;

} // namespace experiments
