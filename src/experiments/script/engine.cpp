/**
 * engine.cpp - ScriptEngine Implementation
 * 
 * Contains the full implementation of ScriptEngine.
 * Requires Node.js and V8 headers.
 */

#include "engine.hpp"

#include <thread>
#include <node/node.h>
#include <node/uv.h>
#include <v8.h>

namespace experiments {

ScriptEngine& ScriptEngine::Instance() {
    static ScriptEngine instance;
    return instance;
}

ScriptEngine::~ScriptEngine() {
    Shutdown(false);
}

bool ScriptEngine::Initialize() {
    std::lock_guard lock(init_mutex_);
    
    if (initialized_.load()) return true;
    
    LOG_INFO("Engine", "Initializing ScriptEngine...");
    
    std::vector<std::string> args = {"embedded"};
    char* argv[] = {(char*)"embedded", nullptr};
    uv_setup_args(1, argv);
    
    auto result = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoInitializeV8,
        node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput
    });
    
    if (result->exit_code() != 0) {
        for (const std::string& error : result->errors()) {
            LOG_ERROR("Engine", "Init error: " + error);
        }
        return false;
    }
    
    args_ = result->args();
    exec_args_ = result->exec_args();
    
    platform_ = node::MultiIsolatePlatform::Create(
        static_cast<int>(std::thread::hardware_concurrency())
    );
    v8::V8::InitializePlatform(platform_.get());
    v8::V8::Initialize();
    
    // Create main environment
    EnvironmentConfig mainConfig;
    mainConfig.name = "Main";
    auto mainEnv = CreateEnvironmentInternal(mainConfig, true);
    if (!mainEnv) {
        LOG_ERROR("Engine", "Failed to create main environment");
        return false;
    }
    
    initialized_.store(true, std::memory_order_release);
    events_.EmitEngine(EngineEvent::Initialized);
    LOG_INFO("Engine", "ScriptEngine initialized");
    return true;
}

void ScriptEngine::Shutdown(bool graceful, std::chrono::milliseconds timeout) {
    std::lock_guard lock(init_mutex_);
    
    if (!initialized_.load()) return;
    
    LOG_INFO("Engine", "Shutting down ScriptEngine...");
    events_.EmitEngine(EngineEvent::ShuttingDown);
    
    // Calculate per-env timeout
    size_t envCount;
    { std::shared_lock lock(env_mutex_); envCount = environments_.size(); }
    auto perEnvTimeout = envCount > 0 ? std::chrono::milliseconds(timeout.count() / envCount) : timeout;
    
    // Stop all environments
    {
        std::unique_lock env_lock(env_mutex_);
        for (auto& [id, env] : environments_) {
            env->Stop(graceful, perEnvTimeout);
        }
        environments_.clear();
    }
    
    // Cleanup V8
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    platform_.reset();
    
    node::TearDownOncePerProcess();
    
    initialized_.store(false, std::memory_order_release);
    events_.EmitEngine(EngineEvent::Shutdown);
    LOG_INFO("Engine", "ScriptEngine shutdown complete");
}

ScriptEnvironmentPtr ScriptEngine::GetMainEnvironment() {
    std::shared_lock lock(env_mutex_);
    auto it = environments_.find(main_env_id_);
    return (it != environments_.end()) ? it->second : nullptr;
}

ScriptEnvironmentPtr ScriptEngine::CreateEnvironment(const EnvironmentConfig& config) {
    return CreateEnvironmentInternal(config, false);
}

ScriptEnvironmentPtr ScriptEngine::CreateEnvironmentInternal(const EnvironmentConfig& config, bool isMain) {
    if (!platform_) return nullptr;
    
    auto id = isMain ? 0 : next_env_id_.fetch_add(1, std::memory_order_relaxed);
    
    EnvironmentConfig cfg = config;
    cfg.name = config.name + "-" + std::to_string(id);
    
    auto env = std::make_shared<ScriptEnvironment>(
        id, platform_.get(), args_, exec_args_, cfg, &events_
    );
    
    if (!env->Initialize() || !env->Start()) {
        return nullptr;
    }
    
    {
        std::unique_lock lock(env_mutex_);
        environments_[id] = env;
        if (isMain) main_env_id_ = id;
    }
    
    return env;
}

ScriptEnvironmentPtr ScriptEngine::GetEnvironment(ScriptEnvironment::EnvironmentId id) {
    std::shared_lock lock(env_mutex_);
    auto it = environments_.find(id);
    return (it != environments_.end()) ? it->second : nullptr;
}

std::vector<ScriptEnvironmentPtr> ScriptEngine::GetAllEnvironments() {
    std::shared_lock lock(env_mutex_);
    std::vector<ScriptEnvironmentPtr> result;
    result.reserve(environments_.size());
    for (const auto& [id, env] : environments_) {
        result.push_back(env);
    }
    return result;
}

bool ScriptEngine::DestroyEnvironment(ScriptEnvironment::EnvironmentId id, bool graceful) {
    if (id == main_env_id_) return false;
    
    std::unique_lock lock(env_mutex_);
    auto it = environments_.find(id);
    if (it == environments_.end()) return false;
    
    it->second->Stop(graceful);
    environments_.erase(it);
    return true;
}

size_t ScriptEngine::GetEnvironmentCount() const {
    std::shared_lock lock(env_mutex_);
    return environments_.size();
}

ScriptPtr ScriptEngine::CreateScript(const std::string& code, const Script::Options& options) {
    return std::make_shared<Script>(code, options);
}

ScriptPtr ScriptEngine::Execute(const std::string& code, const Script::Options& options) {
    auto mainEnv = GetMainEnvironment();
    if (!mainEnv) return nullptr;
    
    auto script = CreateScript(code, options);
    mainEnv->Execute(script);
    return script;
}

Result<std::string> ScriptEngine::ExecuteSync(const std::string& code, std::chrono::milliseconds timeout) {
    auto mainEnv = GetMainEnvironment();
    if (!mainEnv) return ScriptError::Make(ErrorCode::NotInitialized, "Engine not initialized");
    return mainEnv->ExecuteSync(code, timeout);
}

Result<std::string> ScriptEngine::ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout) {
    auto mainEnv = GetMainEnvironment();
    if (!mainEnv) return ScriptError::Make(ErrorCode::NotInitialized, "Engine not initialized");
    return mainEnv->ExecuteFile(path, timeout);
}

} // namespace experiments
