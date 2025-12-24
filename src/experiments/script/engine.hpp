#pragma once

/**
 * engine.hpp - ScriptEngine Class Declaration
 * 
 * The ScriptEngine is the main manager for script execution.
 * Singleton pattern, manages environments and provides high-level API.
 */

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <filesystem>

#include "script_class.hpp"
#include "environment.hpp"
#include "../core/error.hpp"
#include "../core/events.hpp"
#include "../core/logger.hpp"
#include "../config/environment_config.hpp"

// Forward declarations for Node.js types
namespace node {
    class MultiIsolatePlatform;
}

namespace experiments {

//=============================================================================
// ScriptEngine - Core manager with all features
//=============================================================================

class ScriptEngine {
public:
    static ScriptEngine& Instance();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Lifecycle
    bool Initialize();
    void Shutdown(bool graceful = true, std::chrono::milliseconds timeout = std::chrono::seconds(10));
    bool IsInitialized() const { return initialized_.load(std::memory_order_acquire); }

    // Environment management
    ScriptEnvironmentPtr GetMainEnvironment();
    ScriptEnvironmentPtr CreateEnvironment(const EnvironmentConfig& config = EnvironmentConfig::Default());
    ScriptEnvironmentPtr GetEnvironment(ScriptEnvironment::EnvironmentId id);
    std::vector<ScriptEnvironmentPtr> GetAllEnvironments();
    bool DestroyEnvironment(ScriptEnvironment::EnvironmentId id, bool graceful = true);
    size_t GetEnvironmentCount() const;

    // Script execution
    ScriptPtr CreateScript(const std::string& code, const Script::Options& options = {});
    ScriptPtr Execute(const std::string& code, const Script::Options& options = {});
    ScriptResult ExecuteSync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    ScriptResult ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Direct primitive execution (convenience)
    std::optional<double> ExecuteSyncNumber(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    std::optional<std::string> ExecuteSyncString(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    std::optional<bool> ExecuteSyncBool(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
    // Async execution
    std::future<ScriptResult> ExecuteAsync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

    // Events
    EventEmitter& Events() { return events_; }

    // Logging
    void SetLogCallback(Logger::LogCallback callback) { Logger::Instance().SetCallback(std::move(callback)); }
    void SetLogLevel(LogLevel level) { Logger::Instance().SetMinLevel(level); }

private:
    ScriptEngine() = default;
    ~ScriptEngine();

    ScriptEnvironmentPtr CreateEnvironmentInternal(const EnvironmentConfig& config, bool isMain);

    std::unique_ptr<node::MultiIsolatePlatform> platform_;
    std::vector<std::string> args_;
    std::vector<std::string> exec_args_;

    ScriptEnvironment::EnvironmentId main_env_id_{0};
    std::unordered_map<ScriptEnvironment::EnvironmentId, ScriptEnvironmentPtr> environments_;
    mutable std::shared_mutex env_mutex_;

    std::atomic<ScriptEnvironment::EnvironmentId> next_env_id_{1};
    std::atomic<bool> initialized_{false};
    std::mutex init_mutex_;

    EventEmitter events_;
};

} // namespace experiments
