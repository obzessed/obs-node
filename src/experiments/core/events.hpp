#pragma once

/**
 * events.hpp - Event System
 */

#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include <cstdint>

namespace experiments {

enum class EngineEvent {
    Initialized,
    ShuttingDown,
    Shutdown
};

enum class EnvironmentEvent {
    Created,
    Started,
    Stopping,
    Stopped,
    Error
};

enum class ScriptEvent {
    Queued,
    Started,
    Completed,
    Failed,
    Timeout,
    Cancelled
};

class EventEmitter {
public:
    using EngineHandler = std::function<void(EngineEvent)>;
    using EnvHandler = std::function<void(EnvironmentEvent, uint64_t envId)>;
    using ScriptHandler = std::function<void(ScriptEvent, const std::string& scriptId)>;
    
    void OnEngine(EngineHandler handler) {
        std::lock_guard lock(mutex_);
        engine_handlers_.push_back(std::move(handler));
    }
    
    void OnEnvironment(EnvHandler handler) {
        std::lock_guard lock(mutex_);
        env_handlers_.push_back(std::move(handler));
    }
    
    void OnScript(ScriptHandler handler) {
        std::lock_guard lock(mutex_);
        script_handlers_.push_back(std::move(handler));
    }
    
    void EmitEngine(EngineEvent event) {
        std::vector<EngineHandler> handlers;
        { std::lock_guard lock(mutex_); handlers = engine_handlers_; }
        for (auto& h : handlers) h(event);
    }
    
    void EmitEnvironment(EnvironmentEvent event, uint64_t envId) {
        std::vector<EnvHandler> handlers;
        { std::lock_guard lock(mutex_); handlers = env_handlers_; }
        for (auto& h : handlers) h(event, envId);
    }
    
    void EmitScript(ScriptEvent event, const std::string& scriptId) {
        std::vector<ScriptHandler> handlers;
        { std::lock_guard lock(mutex_); handlers = script_handlers_; }
        for (auto& h : handlers) h(event, scriptId);
    }
    
private:
    std::mutex mutex_;
    std::vector<EngineHandler> engine_handlers_;
    std::vector<EnvHandler> env_handlers_;
    std::vector<ScriptHandler> script_handlers_;
};

} // namespace experiments
