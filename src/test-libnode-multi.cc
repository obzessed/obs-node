/**
 * test-libnode-multi.cc - Enhanced ScriptEngine Architecture
 * 
 * Features:
 * - Script Timeout
 * - Logging System
 * - Graceful Shutdown
 * - Event System
 * - Environment Config
 * - Script Priority
 * - Memory Metrics
 * - Execution Metrics
 * - Script Contexts (shared data)
 * - Error Types
 * - File Loading
 */

#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <variant>
#include <filesystem>

#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

// Forward declarations
class ScriptEngine;
class ScriptEnvironment;
class Script;

//=============================================================================
// Logging System
//=============================================================================

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    using LogCallback = std::function<void(LogLevel, const std::string&, const std::string&)>;
    
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }
    
    void SetCallback(LogCallback callback) {
        std::lock_guard lock(mutex_);
        callback_ = std::move(callback);
    }
    
    void SetMinLevel(LogLevel level) { min_level_ = level; }
    
    void Log(LogLevel level, const std::string& category, const std::string& message) {
        if (level < min_level_) return;
        
        std::lock_guard lock(mutex_);
        if (callback_) {
            callback_(level, category, message);
        } else {
            // Default: print to console
            static const char* level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
            std::cout << "[" << level_names[static_cast<int>(level)] << "] "
                      << "[" << category << "] " << message << std::endl;
        }
    }
    
private:
    Logger() = default;
    std::mutex mutex_;
    LogCallback callback_;
    std::atomic<LogLevel> min_level_{LogLevel::Info};
};

#define LOG(level, category, msg) Logger::Instance().Log(level, category, msg)
#define LOG_TRACE(cat, msg) LOG(LogLevel::Trace, cat, msg)
#define LOG_DEBUG(cat, msg) LOG(LogLevel::Debug, cat, msg)
#define LOG_INFO(cat, msg) LOG(LogLevel::Info, cat, msg)
#define LOG_WARN(cat, msg) LOG(LogLevel::Warn, cat, msg)
#define LOG_ERROR(cat, msg) LOG(LogLevel::Error, cat, msg)

//=============================================================================
// Error Types
//=============================================================================

enum class ErrorCode {
    None = 0,
    NotInitialized,
    AlreadyInitialized,
    EnvironmentCreationFailed,
    CompileError,
    RuntimeError,
    Timeout,
    Cancelled,
    FileNotFound,
    FileReadError,
    InvalidArgument,
    InternalError
};

struct ScriptError {
    ErrorCode code{ErrorCode::None};
    std::string message;
    std::string stack;
    int line{0};
    int column{0};
    
    bool IsError() const { return code != ErrorCode::None; }
    operator bool() const { return IsError(); }
    
    static ScriptError None() { return {}; }
    
    static ScriptError Make(ErrorCode code, const std::string& msg) {
        return {code, msg, "", 0, 0};
    }
};

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(ScriptError error) : data_(std::move(error)) {}
    
    bool IsOk() const { return std::holds_alternative<T>(data_); }
    bool IsError() const { return std::holds_alternative<ScriptError>(data_); }
    
    T& Value() { return std::get<T>(data_); }
    const T& Value() const { return std::get<T>(data_); }
    ScriptError& Error() { return std::get<ScriptError>(data_); }
    const ScriptError& Error() const { return std::get<ScriptError>(data_); }
    
    T ValueOr(T default_value) const {
        return IsOk() ? Value() : default_value;
    }
    
private:
    std::variant<T, ScriptError> data_;
};

//=============================================================================
// Event System
//=============================================================================

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

//=============================================================================
// Metrics
//=============================================================================

struct MemoryMetrics {
    size_t heap_size_limit{0};
    size_t total_heap_size{0};
    size_t used_heap_size{0};
    size_t external_memory{0};
    
    double HeapUsagePercent() const {
        return heap_size_limit > 0 ? (100.0 * used_heap_size / heap_size_limit) : 0;
    }
};

struct ExecutionMetrics {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::chrono::milliseconds duration{0};
    size_t scripts_executed{0};
    size_t scripts_failed{0};
    size_t scripts_timed_out{0};
};

struct EnvironmentMetrics {
    MemoryMetrics memory;
    ExecutionMetrics execution;
    size_t queue_size{0};
    bool is_running{false};
};

//=============================================================================
// Environment Config
//=============================================================================

// Forward declaration
class ModuleLoader;
using ModuleLoaderPtr = std::shared_ptr<ModuleLoader>;

//=============================================================================
// Script Permissions System
//=============================================================================

// Individual permission flags
enum class ScriptPermission : uint32_t {
    None            = 0,
    
    // File system
    FileRead        = 1 << 0,   // Read files
    FileWrite       = 1 << 1,   // Write/create files
    FileDelete      = 1 << 2,   // Delete files
    FileSystem      = FileRead | FileWrite | FileDelete,
    
    // Network
    NetConnect      = 1 << 3,   // Connect to remote hosts
    NetListen       = 1 << 4,   // Listen on ports
    NetHttp         = 1 << 5,   // HTTP requests
    NetWebSocket    = 1 << 6,   // WebSocket connections
    Network         = NetConnect | NetListen | NetHttp | NetWebSocket,
    
    // Process/System
    ProcessSpawn    = 1 << 7,   // Spawn child processes
    ProcessEnv      = 1 << 8,   // Access environment variables
    ProcessExit     = 1 << 9,   // Call process.exit()
    Process         = ProcessSpawn | ProcessEnv | ProcessExit,
    
    // OBS-specific
    ObsScenes       = 1 << 10,  // Scene manipulation
    ObsSources      = 1 << 11,  // Source manipulation
    ObsStreaming    = 1 << 12,  // Start/stop streaming
    ObsRecording    = 1 << 13,  // Start/stop recording
    ObsSettings     = 1 << 14,  // Modify OBS settings
    ObsHotkeys      = 1 << 15,  // Trigger hotkeys
    ObsAll          = ObsScenes | ObsSources | ObsStreaming | ObsRecording | ObsSettings | ObsHotkeys,
    
    // Module system
    ModuleLoad      = 1 << 16,  // Load native modules
    ModuleRequire   = 1 << 17,  // Use require()
    ModuleImport    = 1 << 18,  // Use import
    Modules         = ModuleLoad | ModuleRequire | ModuleImport,
    
    // Timers
    Timers          = 1 << 19,  // setTimeout, setInterval
    
    // Crypto
    Crypto          = 1 << 20,  // Crypto APIs
    
    // Special
    Eval            = 1 << 21,  // Use eval() and Function()
    Unsafe          = 1 << 22,  // Mark as unsafe (for auditing)
    
    // Presets
    Safe            = Timers | Crypto,  // Minimal safe permissions
    Standard        = Safe | ModuleRequire | ModuleImport | ProcessEnv,  // Standard script
    Trusted         = Standard | FileRead | NetHttp | ObsScenes | ObsSources,  // Trusted script
    Full            = 0xFFFFFFFF  // All permissions (dangerous)
};

// Bitwise operators for ScriptPermission
inline ScriptPermission operator|(ScriptPermission a, ScriptPermission b) {
    return static_cast<ScriptPermission>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ScriptPermission operator&(ScriptPermission a, ScriptPermission b) {
    return static_cast<ScriptPermission>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ScriptPermission operator~(ScriptPermission a) {
    return static_cast<ScriptPermission>(~static_cast<uint32_t>(a));
}

inline ScriptPermission& operator|=(ScriptPermission& a, ScriptPermission b) {
    return a = a | b;
}

inline ScriptPermission& operator&=(ScriptPermission& a, ScriptPermission b) {
    return a = a & b;
}

// Permission set with named operations
class PermissionSet {
public:
    PermissionSet() : flags_(ScriptPermission::None) {}
    explicit PermissionSet(ScriptPermission flags) : flags_(flags) {}
    
    // Grant permissions
    PermissionSet& Grant(ScriptPermission perm) {
        flags_ |= perm;
        return *this;
    }
    
    // Revoke permissions
    PermissionSet& Revoke(ScriptPermission perm) {
        flags_ &= ~perm;
        return *this;
    }
    
    // Check if has permission
    bool Has(ScriptPermission perm) const {
        return (flags_ & perm) == perm;
    }
    
    // Check if has any of the permissions
    bool HasAny(ScriptPermission perm) const {
        return (flags_ & perm) != ScriptPermission::None;
    }
    
    // Get raw flags
    ScriptPermission Flags() const { return flags_; }
    
    // Set from flags
    void SetFlags(ScriptPermission flags) { flags_ = flags; }
    
    // Merge with another set (union)
    PermissionSet& Merge(const PermissionSet& other) {
        flags_ |= other.flags_;
        return *this;
    }
    
    // Intersect with another set
    PermissionSet& Intersect(const PermissionSet& other) {
        flags_ &= other.flags_;
        return *this;
    }
    
    // Clear all
    void Clear() { flags_ = ScriptPermission::None; }
    
    // Check if empty
    bool IsEmpty() const { return flags_ == ScriptPermission::None; }
    
    // Preset factories
    static PermissionSet Safe() { return PermissionSet(ScriptPermission::Safe); }
    static PermissionSet Standard() { return PermissionSet(ScriptPermission::Standard); }
    static PermissionSet Trusted() { return PermissionSet(ScriptPermission::Trusted); }
    static PermissionSet Full() { return PermissionSet(ScriptPermission::Full); }
    static PermissionSet None() { return PermissionSet(ScriptPermission::None); }
    
    // Pretty print for debugging
    std::string ToString() const {
        if (flags_ == ScriptPermission::None) return "None";
        if (flags_ == ScriptPermission::Full) return "Full";
        
        std::vector<std::string> parts;
        if (Has(ScriptPermission::FileRead)) parts.push_back("FileRead");
        if (Has(ScriptPermission::FileWrite)) parts.push_back("FileWrite");
        if (Has(ScriptPermission::FileDelete)) parts.push_back("FileDelete");
        if (Has(ScriptPermission::NetConnect)) parts.push_back("NetConnect");
        if (Has(ScriptPermission::NetListen)) parts.push_back("NetListen");
        if (Has(ScriptPermission::NetHttp)) parts.push_back("NetHttp");
        if (Has(ScriptPermission::ProcessSpawn)) parts.push_back("ProcessSpawn");
        if (Has(ScriptPermission::ProcessEnv)) parts.push_back("ProcessEnv");
        if (Has(ScriptPermission::ObsScenes)) parts.push_back("ObsScenes");
        if (Has(ScriptPermission::ObsSources)) parts.push_back("ObsSources");
        if (Has(ScriptPermission::ObsStreaming)) parts.push_back("ObsStreaming");
        if (Has(ScriptPermission::ObsRecording)) parts.push_back("ObsRecording");
        if (Has(ScriptPermission::ModuleRequire)) parts.push_back("ModuleRequire");
        if (Has(ScriptPermission::Timers)) parts.push_back("Timers");
        if (Has(ScriptPermission::Eval)) parts.push_back("Eval");
        
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += " | ";
            result += parts[i];
        }
        return result;
    }
    
private:
    ScriptPermission flags_;
};

//=============================================================================
// Environment Configuration
//=============================================================================

struct EnvironmentConfig {
    std::string name{"Environment"};
    
    // Memory limits
    size_t max_heap_size_mb{512};  // V8 heap limit in MB
    
    // Execution
    std::chrono::milliseconds default_script_timeout{30000};  // 30 seconds
    bool allow_file_access{true};
    
    // Module resolution
    std::vector<std::string> module_paths;
    std::string working_directory;
    ModuleLoaderPtr module_loader;  // Custom module loader
    
    // Bootstrap
    std::string bootstrap_script;
    
    // Console
    bool capture_console{true};
    
    // Permissions - environment-level defaults
    PermissionSet permissions{PermissionSet::Standard()};
    
    // Allowed/denied module patterns
    std::vector<std::string> allowed_modules;   // Whitelist (empty = allow all)
    std::vector<std::string> denied_modules;    // Blacklist
    
    // Allowed/denied file paths
    std::vector<std::string> allowed_paths;     // Whitelist for file access
    std::vector<std::string> denied_paths;      // Blacklist for file access
    
    // Allowed/denied network hosts
    std::vector<std::string> allowed_hosts;     // Whitelist for network
    std::vector<std::string> denied_hosts;      // Blacklist for network
    
    static EnvironmentConfig Default() {
        return {};
    }
    
    static EnvironmentConfig Sandboxed() {
        EnvironmentConfig config;
        config.name = "Sandboxed";
        config.permissions = PermissionSet::Safe();
        config.allow_file_access = false;
        return config;
    }
    
    static EnvironmentConfig Trusted() {
        EnvironmentConfig config;
        config.name = "Trusted";
        config.permissions = PermissionSet::Trusted();
        return config;
    }
};

//=============================================================================
// Script Priority
//=============================================================================

enum class ScriptPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

//=============================================================================
// Script Context (shared data)
//=============================================================================

class ScriptContext {
public:
    void Set(const std::string& key, const std::string& value) {
        std::lock_guard lock(mutex_);
        data_[key] = value;
    }
    
    std::optional<std::string> Get(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? std::optional{it->second} : std::nullopt;
    }
    
    void Remove(const std::string& key) {
        std::lock_guard lock(mutex_);
        data_.erase(key);
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        data_.clear();
    }
    
    std::unordered_map<std::string, std::string> GetAll() const {
        std::shared_lock lock(mutex_);
        return data_;
    }
    
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

using ScriptContextPtr = std::shared_ptr<ScriptContext>;

//=============================================================================
// Module System - Separated Resolvers and Loaders
//=============================================================================

// Forward declaration
struct SourceMap;

enum class ModuleFormat {
    CommonJS,   // require()
    ESModule,   // import/export
    JSON,       // JSON data
    Unknown     // Auto-detect based on extension or content
};

struct ModuleInfo {
    std::string specifier;              // Original import/require specifier
    std::string resolved_path;          // Resolved path/URL
    std::string source;                 // Module source code (possibly transformed)
    std::string original_source;        // Original source before transformation
    ModuleFormat format{ModuleFormat::Unknown};
    bool transformed{false};            // Was the source transformed?
    std::optional<SourceMap> source_map; // Optional source map (forward declaration issue - placeholder)
    
    bool is_valid() const { return !source.empty(); }
    bool was_transformed() const { return transformed && !original_source.empty(); }
};

// Helper functions for format detection
namespace ModuleUtils {
    inline ModuleFormat DetectFormatFromPath(const std::string& path) {
        if (path.ends_with(".mjs") || path.ends_with(".mts")) {
            return ModuleFormat::ESModule;
        } else if (path.ends_with(".cjs") || path.ends_with(".cts")) {
            return ModuleFormat::CommonJS;
        } else if (path.ends_with(".json")) {
            return ModuleFormat::JSON;
        } else if (path.ends_with(".js") || path.ends_with(".ts")) {
            return ModuleFormat::CommonJS;
        }
        return ModuleFormat::Unknown;
    }
    
    inline ModuleFormat DetectFormatFromContent(const std::string& source) {
        if (source.find("import ") != std::string::npos || 
            source.find("export ") != std::string::npos) {
            return ModuleFormat::ESModule;
        }
        return ModuleFormat::CommonJS;
    }
}

//=============================================================================
// Module Resolver - Abstract base class for path resolution
//=============================================================================

struct ResolveResult {
    std::string resolved_path;
    std::string resolver_name;  // Which resolver found it
    bool is_virtual{false};     // Is this a virtual module?
    
    bool is_valid() const { return !resolved_path.empty(); }
    operator bool() const { return is_valid(); }
};

class ModuleResolver {
public:
    virtual ~ModuleResolver() = default;
    
    // Resolve a module specifier to full path/URL
    virtual std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) = 0;
    
    // Check if this resolver can handle the specifier
    virtual bool CanHandle(const std::string& specifier) const = 0;
    
    // Get resolver name for logging
    virtual std::string GetName() const = 0;
};

using ModuleResolverPtr = std::shared_ptr<ModuleResolver>;

//=============================================================================
// Disk Resolver - Resolves modules from file system
//=============================================================================

class DiskResolver : public ModuleResolver {
public:
    explicit DiskResolver(std::vector<std::string> search_paths = {"."}) 
        : search_paths_(std::move(search_paths)) {}
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path
    ) override {
        namespace fs = std::filesystem;
        
        std::optional<std::string> resolved;
        
        // Absolute path
        if (fs::path(specifier).is_absolute()) {
            resolved = TryResolve(specifier);
        }
        // Relative path
        else if (specifier.starts_with("./") || specifier.starts_with("../")) {
            fs::path parent_dir = parent_path.empty() 
                ? fs::current_path() 
                : fs::path(parent_path).parent_path();
            resolved = TryResolve((parent_dir / specifier).string());
        }
        // Bare specifier - search in paths
        else {
            for (const auto& search_path : search_paths_) {
                resolved = TryResolve((fs::path(search_path) / specifier).string());
                if (resolved) break;
            }
        }
        
        if (resolved) {
            return ResolveResult{*resolved, GetName(), false};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return !specifier.starts_with("http://") && 
               !specifier.starts_with("https://") &&
               !specifier.starts_with("virtual:");
    }
    
    std::string GetName() const override { return "DiskResolver"; }
    
    void AddSearchPath(const std::string& path) {
        search_paths_.push_back(path);
    }
    
    const std::vector<std::string>& GetSearchPaths() const {
        return search_paths_;
    }
    
private:
    std::vector<std::string> search_paths_;
    
    std::optional<std::string> TryResolve(const std::string& path) {
        namespace fs = std::filesystem;
        
        // Direct file
        if (fs::exists(path) && fs::is_regular_file(path)) {
            return fs::absolute(path).string();
        }
        
        // Try adding extensions
        static const std::vector<std::string> extensions = {
            ".js", ".mjs", ".cjs", ".json", ".node", ".ts", ".mts", ".cts"
        };
        for (const auto& ext : extensions) {
            std::string with_ext = path + ext;
            if (fs::exists(with_ext) && fs::is_regular_file(with_ext)) {
                return fs::absolute(with_ext).string();
            }
        }
        
        // Try as directory with index file
        if (fs::exists(path) && fs::is_directory(path)) {
            for (const auto& ext : extensions) {
                std::string index_path = (fs::path(path) / ("index" + ext)).string();
                if (fs::exists(index_path)) {
                    return fs::absolute(index_path).string();
                }
            }
            // Also check package.json main field (simplified)
            std::string pkg_path = (fs::path(path) / "package.json").string();
            if (fs::exists(pkg_path)) {
                // In production, parse package.json for "main" field
                return TryResolve((fs::path(path) / "index.js").string());
            }
        }
        
        return std::nullopt;
    }
};

//=============================================================================
// Virtual Resolver - Resolves modules from in-memory registry
//=============================================================================

class VirtualResolver : public ModuleResolver {
public:
    explicit VirtualResolver(std::string prefix = "virtual:")
        : prefix_(std::move(prefix)) {}
    
    // Register a virtual module path
    void Register(const std::string& name) {
        std::lock_guard lock(mutex_);
        registered_.insert(name);
    }
    
    void Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        registered_.erase(name);
    }
    
    bool Has(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return registered_.contains(name);
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        std::shared_lock lock(mutex_);
        
        std::string name = specifier;
        if (name.starts_with(prefix_)) {
            name = name.substr(prefix_.length());
        }
        
        if (registered_.contains(name)) {
            return ResolveResult{prefix_ + name, GetName(), true};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        if (specifier.starts_with(prefix_)) return true;
        std::shared_lock lock(mutex_);
        return registered_.contains(specifier);
    }
    
    std::string GetName() const override { return "VirtualResolver"; }
    std::string GetPrefix() const { return prefix_; }
    void SetPrefix(std::string prefix) { prefix_ = std::move(prefix); }
    
private:
    std::string prefix_;
    mutable std::shared_mutex mutex_;
    std::unordered_set<std::string> registered_;
};

//=============================================================================
// Remote Resolver - Resolves HTTP/HTTPS URLs
//=============================================================================

class RemoteResolver : public ModuleResolver {
public:
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        if (specifier.starts_with("http://") || specifier.starts_with("https://")) {
            // Check allowed prefixes
            if (!allowed_prefixes_.empty()) {
                bool allowed = false;
                for (const auto& prefix : allowed_prefixes_) {
                    if (specifier.starts_with(prefix)) {
                        allowed = true;
                        break;
                    }
                }
                if (!allowed) {
                    LOG_WARN("RemoteResolver", "URL not in allowed prefixes: " + specifier);
                    return std::nullopt;
                }
            }
            return ResolveResult{specifier, GetName(), false};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("http://") || specifier.starts_with("https://");
    }
    
    std::string GetName() const override { return "RemoteResolver"; }
    
    void SetAllowedPrefixes(std::vector<std::string> prefixes) {
        allowed_prefixes_ = std::move(prefixes);
    }
    
private:
    std::vector<std::string> allowed_prefixes_;
};

//=============================================================================
// NPM Resolver - Resolves npm: URLs (npm:package@version/path)
//=============================================================================

struct NpmPackageSpec {
    std::string name;           // Package name (may include scope like @org/pkg)
    std::string version;        // Version or tag (e.g., "1.0.0", "latest")
    std::string subpath;        // Import subpath (e.g., "/dist/index.js")
    
    static std::optional<NpmPackageSpec> Parse(const std::string& specifier) {
        if (!specifier.starts_with("npm:")) return std::nullopt;
        
        std::string rest = specifier.substr(4);  // Remove "npm:"
        NpmPackageSpec spec;
        
        // Handle scoped packages (@scope/name)
        size_t name_end = 0;
        if (rest.starts_with("@")) {
            size_t slash_pos = rest.find('/');
            if (slash_pos == std::string::npos) return std::nullopt;
            name_end = rest.find_first_of("/@", slash_pos + 1);
        } else {
            name_end = rest.find_first_of("/@");
        }
        
        if (name_end == std::string::npos) {
            spec.name = rest;
            spec.version = "latest";
            return spec;
        }
        
        spec.name = rest.substr(0, name_end);
        rest = rest.substr(name_end);
        
        // Parse version if present
        if (rest.starts_with("@")) {
            size_t version_end = rest.find('/');
            if (version_end == std::string::npos) {
                spec.version = rest.substr(1);
            } else {
                spec.version = rest.substr(1, version_end - 1);
                spec.subpath = rest.substr(version_end);
            }
        } else if (rest.starts_with("/")) {
            spec.version = "latest";
            spec.subpath = rest;
        }
        
        if (spec.version.empty()) spec.version = "latest";
        return spec;
    }
};

class NpmResolver : public ModuleResolver {
public:
    // Set the registry URL (default: https://registry.npmjs.org)
    void SetRegistry(std::string registry) {
        registry_ = std::move(registry);
        if (!registry_.ends_with("/")) registry_ += "/";
    }
    
    // Set local node_modules path for offline resolution
    void SetNodeModulesPath(std::string path) {
        node_modules_path_ = std::move(path);
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        auto pkg = NpmPackageSpec::Parse(specifier);
        if (!pkg) return std::nullopt;
        
        // Try local node_modules first
        if (!node_modules_path_.empty()) {
            namespace fs = std::filesystem;
            fs::path local_path = fs::path(node_modules_path_) / pkg->name;
            if (fs::exists(local_path)) {
                std::string resolved = local_path.string();
                if (!pkg->subpath.empty()) {
                    resolved += pkg->subpath;
                }
                return ResolveResult{resolved, GetName(), false};
            }
        }
        
        // Build CDN URL (using esm.sh, unpkg, or similar)
        // Format: https://esm.sh/package@version/subpath
        std::string cdn_url = cdn_base_ + pkg->name;
        if (pkg->version != "latest") {
            cdn_url += "@" + pkg->version;
        }
        if (!pkg->subpath.empty()) {
            cdn_url += pkg->subpath;
        }
        
        return ResolveResult{cdn_url, GetName(), false};
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("npm:");
    }
    
    std::string GetName() const override { return "NpmResolver"; }
    
    // Set CDN base URL (default: https://esm.sh/)
    void SetCdnBase(std::string cdn) {
        cdn_base_ = std::move(cdn);
        if (!cdn_base_.ends_with("/")) cdn_base_ += "/";
    }
    
private:
    std::string registry_{"https://registry.npmjs.org/"};
    std::string node_modules_path_;
    std::string cdn_base_{"https://esm.sh/"};
};

//=============================================================================
// JSR Resolver - Resolves jsr: URLs (jsr:@scope/package@version/path)
//=============================================================================

struct JsrPackageSpec {
    std::string scope;          // Package scope (e.g., "@std")
    std::string name;           // Package name (e.g., "path")
    std::string version;        // Version (e.g., "1.0.0")
    std::string subpath;        // Import subpath (e.g., "/mod.ts")
    
    static std::optional<JsrPackageSpec> Parse(const std::string& specifier) {
        if (!specifier.starts_with("jsr:")) return std::nullopt;
        
        std::string rest = specifier.substr(4);  // Remove "jsr:"
        JsrPackageSpec spec;
        
        // JSR packages must be scoped (@scope/name)
        if (!rest.starts_with("@")) return std::nullopt;
        
        // Parse scope
        size_t slash_pos = rest.find('/');
        if (slash_pos == std::string::npos) return std::nullopt;
        spec.scope = rest.substr(0, slash_pos);
        rest = rest.substr(slash_pos + 1);
        
        // Parse name and version
        size_t at_pos = rest.find('@');
        size_t path_pos = rest.find('/');
        
        if (at_pos != std::string::npos && (path_pos == std::string::npos || at_pos < path_pos)) {
            spec.name = rest.substr(0, at_pos);
            rest = rest.substr(at_pos + 1);
            
            path_pos = rest.find('/');
            if (path_pos != std::string::npos) {
                spec.version = rest.substr(0, path_pos);
                spec.subpath = rest.substr(path_pos);
            } else {
                spec.version = rest;
            }
        } else if (path_pos != std::string::npos) {
            spec.name = rest.substr(0, path_pos);
            spec.subpath = rest.substr(path_pos);
        } else {
            spec.name = rest;
        }
        
        return spec;
    }
};

class JsrResolver : public ModuleResolver {
public:
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        auto pkg = JsrPackageSpec::Parse(specifier);
        if (!pkg) return std::nullopt;
        
        // Build JSR CDN URL
        // Format: https://jsr.io/@scope/name@version/subpath
        std::string url = jsr_base_ + pkg->scope + "/" + pkg->name;
        if (!pkg->version.empty()) {
            url += "@" + pkg->version;
        }
        if (!pkg->subpath.empty()) {
            url += pkg->subpath;
        } else {
            url += "/mod.ts";  // Default entry point for JSR packages
        }
        
        return ResolveResult{url, GetName(), false};
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("jsr:");
    }
    
    std::string GetName() const override { return "JsrResolver"; }
    
    // Set JSR base URL (default: https://jsr.io/)
    void SetJsrBase(std::string base) {
        jsr_base_ = std::move(base);
        if (!jsr_base_.ends_with("/")) jsr_base_ += "/";
    }
    
private:
    std::string jsr_base_{"https://jsr.io/"};
};

//=============================================================================
// Source Map - Represents a JavaScript/TypeScript source map
//=============================================================================

struct SourceMap {
    int version{3};
    std::string file;
    std::string source_root;
    std::vector<std::string> sources;
    std::vector<std::string> sources_content;
    std::vector<std::string> names;
    std::string mappings;
    
    bool is_valid() const { return version == 3 && !mappings.empty(); }
    
    // Generate inline source map URL
    std::string ToInlineUrl() const {
        // In production, Base64 encode the JSON
        std::string json = ToJson();
        // Placeholder: would need Base64 encoding
        return "//# sourceMappingURL=data:application/json;base64," + json;
    }
    
    // Convert to JSON string
    std::string ToJson() const {
        std::ostringstream oss;
        oss << R"({"version":)" << version;
        oss << R"(,"file":")" << file << "\"";
        if (!source_root.empty()) {
            oss << R"(,"sourceRoot":")" << source_root << "\"";
        }
        oss << R"(,"sources":[)";
        for (size_t i = 0; i < sources.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << sources[i] << "\"";
        }
        oss << R"(],"names":[)";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << names[i] << "\"";
        }
        oss << R"(],"mappings":")" << mappings << "\"}";
        return oss.str();
    }
    
    // Parse from JSON (simplified)
    static std::optional<SourceMap> FromJson(const std::string& json) {
        // Placeholder: would need proper JSON parsing
        SourceMap map;
        map.version = 3;
        // In production, parse the JSON properly
        return map;
    }
};

//=============================================================================
// Transform Result - Output from a module transformer
//=============================================================================

struct TransformResult {
    std::string code;                       // Transformed source code
    std::optional<SourceMap> source_map;    // Optional source map
    std::vector<std::string> errors;        // Transform errors
    std::vector<std::string> warnings;      // Transform warnings
    
    bool is_ok() const { return errors.empty(); }
    
    // Apply source map inline
    std::string GetCodeWithInlineSourceMap() const {
        if (source_map && source_map->is_valid()) {
            return code + "\n" + source_map->ToInlineUrl();
        }
        return code;
    }
};

//=============================================================================
// Module Transformer - Abstract base for code transformations
//=============================================================================

class ModuleTransformer {
public:
    virtual ~ModuleTransformer() = default;
    
    // Transform source code
    virtual TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat format
    ) = 0;
    
    // Check if transformer should handle this file
    virtual bool ShouldTransform(const std::string& filename) const = 0;
    
    // Get transformer name
    virtual std::string GetName() const = 0;
};

using ModuleTransformerPtr = std::shared_ptr<ModuleTransformer>;

//=============================================================================
// TypeScript Transformer - Strips types and transforms TS to JS
//=============================================================================

class TypeScriptTransformer : public ModuleTransformer {
public:
    struct Options {
        bool jsx{false};                // Enable JSX support
        bool emit_decorator_metadata{false};
        bool use_define_for_class_fields{true};
        std::string jsx_factory{"React.createElement"};
        std::string jsx_fragment{"React.Fragment"};
        bool generate_source_map{true};
    };
    
    explicit TypeScriptTransformer(Options options = {})
        : options_(std::move(options)) {}
    
    TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat /*format*/
    ) override {
        TransformResult result;
        
        // Placeholder: In production, use SWC, esbuild, or TypeScript compiler API
        // This is a simplified type-stripping implementation for demonstration
        
        LOG_INFO("TypeScriptTransformer", "Transforming: " + filename);
        
        // For now, just pass through (in reality, strip types)
        result.code = StripTypes(source);
        
        if (options_.generate_source_map) {
            result.source_map = GenerateSourceMap(source, result.code, filename);
        }
        
        return result;
    }
    
    bool ShouldTransform(const std::string& filename) const override {
        return filename.ends_with(".ts") || 
               filename.ends_with(".tsx") ||
               filename.ends_with(".mts") ||
               filename.ends_with(".cts");
    }
    
    std::string GetName() const override { return "TypeScriptTransformer"; }
    
    Options& GetOptions() { return options_; }
    
private:
    Options options_;
    
    // Simple type stripping (placeholder - use real parser in production)
    std::string StripTypes(const std::string& source) {
        // This is a very simplified placeholder
        // Real implementation would use a proper TypeScript parser
        std::string result = source;
        
        // Remove type annotations like ": string", ": number", etc.
        // Remove interface definitions, type aliases, etc.
        // This is just for demonstration - not suitable for production
        
        return result;
    }
    
    SourceMap GenerateSourceMap(
        const std::string& /*original*/, 
        const std::string& /*transformed*/,
        const std::string& filename
    ) {
        SourceMap map;
        map.version = 3;
        map.file = filename;
        map.sources.push_back(filename);
        map.mappings = "AAAA";  // Placeholder - would need proper VLQ encoding
        return map;
    }
};

//=============================================================================
// Transformer Chain - Applies multiple transformers in sequence
//=============================================================================

class TransformerChain : public ModuleTransformer {
public:
    void AddTransformer(ModuleTransformerPtr transformer) {
        transformers_.push_back(std::move(transformer));
    }
    
    TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat format
    ) override {
        TransformResult result;
        result.code = source;
        
        for (const auto& transformer : transformers_) {
            if (transformer->ShouldTransform(filename)) {
                auto step_result = transformer->Transform(result.code, filename, format);
                
                // Merge errors and warnings
                result.errors.insert(result.errors.end(), 
                    step_result.errors.begin(), step_result.errors.end());
                result.warnings.insert(result.warnings.end(), 
                    step_result.warnings.begin(), step_result.warnings.end());
                
                if (!step_result.is_ok()) break;
                
                result.code = step_result.code;
                if (step_result.source_map) {
                    // In production, merge source maps properly
                    result.source_map = step_result.source_map;
                }
            }
        }
        
        return result;
    }
    
    bool ShouldTransform(const std::string& filename) const override {
        for (const auto& transformer : transformers_) {
            if (transformer->ShouldTransform(filename)) return true;
        }
        return false;
    }
    
    std::string GetName() const override { return "TransformerChain"; }
    
private:
    std::vector<ModuleTransformerPtr> transformers_;
};


//=============================================================================
// Resolver Chain - Combines multiple resolvers in priority order
//=============================================================================

class ResolverChain : public ModuleResolver {
public:
    ResolverChain() = default;
    
    explicit ResolverChain(std::vector<ModuleResolverPtr> resolvers) 
        : resolvers_(std::move(resolvers)) {}
    
    void AddResolver(ModuleResolverPtr resolver) {
        resolvers_.push_back(std::move(resolver));
    }
    
    void AddResolverFirst(ModuleResolverPtr resolver) {
        resolvers_.insert(resolvers_.begin(), std::move(resolver));
    }
    
    void RemoveResolver(const std::string& name) {
        resolvers_.erase(
            std::remove_if(resolvers_.begin(), resolvers_.end(), 
                [&name](const auto& r) { return r->GetName() == name; }),
            resolvers_.end()
        );
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path
    ) override {
        for (const auto& resolver : resolvers_) {
            if (resolver->CanHandle(specifier)) {
                auto result = resolver->Resolve(specifier, parent_path);
                if (result) {
                    LOG_DEBUG("ResolverChain", 
                        result->resolver_name + " resolved: " + specifier + " -> " + result->resolved_path);
                    return result;
                }
            }
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        for (const auto& resolver : resolvers_) {
            if (resolver->CanHandle(specifier)) return true;
        }
        return false;
    }
    
    std::string GetName() const override { return "ResolverChain"; }
    
    std::vector<std::string> GetResolverNames() const {
        std::vector<std::string> names;
        for (const auto& resolver : resolvers_) {
            names.push_back(resolver->GetName());
        }
        return names;
    }
    
private:
    std::vector<ModuleResolverPtr> resolvers_;
};

//=============================================================================
// Module Loader - Abstract base class for loading module content
//=============================================================================

class ModuleLoader {
public:
    virtual ~ModuleLoader() = default;
    
    // Load module from resolved path (returns source code)
    virtual std::optional<ModuleInfo> Load(const std::string& resolved_path) = 0;
    
    // Check if this loader can load from the resolved path
    virtual bool CanLoad(const std::string& resolved_path) const = 0;
    
    // Get loader name
    virtual std::string GetName() const = 0;
};

using ModuleLoaderPtr = std::shared_ptr<ModuleLoader>;

//=============================================================================
// Disk Loader - Loads module content from file system
//=============================================================================

class DiskLoader : public ModuleLoader {
public:
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        std::ifstream file(resolved_path);
        if (!file.is_open()) return std::nullopt;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        
        ModuleInfo info;
        info.resolved_path = resolved_path;
        info.source = buffer.str();
        info.format = ModuleUtils::DetectFormatFromPath(resolved_path);
        if (info.format == ModuleFormat::Unknown) {
            info.format = ModuleUtils::DetectFormatFromContent(info.source);
        }
        
        return info;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        return !resolved_path.starts_with("http://") && 
               !resolved_path.starts_with("https://") &&
               !resolved_path.starts_with("virtual:");
    }
    
    std::string GetName() const override { return "DiskLoader"; }
};

//=============================================================================
// Virtual Loader - Loads module content from in-memory storage
//=============================================================================

class VirtualLoader : public ModuleLoader {
public:
    explicit VirtualLoader(std::string prefix = "virtual:")
        : prefix_(std::move(prefix)) {}
    
    // Register module content
    void Register(const std::string& name, const std::string& source, 
                  ModuleFormat format = ModuleFormat::CommonJS) {
        std::lock_guard lock(mutex_);
        modules_[name] = {name, prefix_ + name, source, format};
    }
    
    void RegisterAll(const std::unordered_map<std::string, std::string>& modules) {
        for (const auto& [name, source] : modules) {
            Register(name, source);
        }
    }
    
    void Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        modules_.erase(name);
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        modules_.clear();
    }
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        std::shared_lock lock(mutex_);
        
        std::string name = resolved_path;
        if (name.starts_with(prefix_)) {
            name = name.substr(prefix_.length());
        }
        
        auto it = modules_.find(name);
        if (it != modules_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        if (resolved_path.starts_with(prefix_)) return true;
        std::shared_lock lock(mutex_);
        return modules_.contains(resolved_path);
    }
    
    std::string GetName() const override { return "VirtualLoader"; }
    std::string GetPrefix() const { return prefix_; }
    
    std::vector<std::string> ListModules() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : modules_) {
            names.push_back(name);
        }
        return names;
    }
    
    bool Has(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return modules_.contains(name);
    }
    
    size_t Count() const {
        std::shared_lock lock(mutex_);
        return modules_.size();
    }
    
private:
    std::string prefix_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ModuleInfo> modules_;
};

//=============================================================================
// Remote Loader - Loads module content from HTTP/HTTPS
//=============================================================================

class RemoteLoader : public ModuleLoader {
public:
    struct CacheEntry {
        ModuleInfo info;
        std::chrono::steady_clock::time_point cached_at;
    };
    
    explicit RemoteLoader(std::chrono::seconds cache_ttl = std::chrono::seconds(3600))
        : cache_ttl_(cache_ttl) {}
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        // Check cache first
        {
            std::shared_lock lock(cache_mutex_);
            auto it = cache_.find(resolved_path);
            if (it != cache_.end()) {
                auto age = std::chrono::steady_clock::now() - it->second.cached_at;
                if (age < cache_ttl_) {
                    return it->second.info;
                }
            }
        }
        
        // Fetch from remote
        auto source = FetchUrl(resolved_path);
        if (!source) return std::nullopt;
        
        ModuleInfo info;
        info.resolved_path = resolved_path;
        info.source = *source;
        info.format = ModuleUtils::DetectFormatFromPath(resolved_path);
        if (info.format == ModuleFormat::Unknown) {
            info.format = ModuleUtils::DetectFormatFromContent(info.source);
        }
        
        // Cache the result
        {
            std::lock_guard lock(cache_mutex_);
            cache_[resolved_path] = {info, std::chrono::steady_clock::now()};
        }
        
        return info;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        return resolved_path.starts_with("http://") || resolved_path.starts_with("https://");
    }
    
    std::string GetName() const override { return "RemoteLoader"; }
    
    void ClearCache() {
        std::lock_guard lock(cache_mutex_);
        cache_.clear();
    }
    
private:
    std::chrono::seconds cache_ttl_;
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
    
    std::optional<std::string> FetchUrl(const std::string& url) {
        // Placeholder - implement with libcurl in production
        LOG_INFO("RemoteLoader", "Would fetch: " + url);
        return std::nullopt;
    }
};

//=============================================================================
// Loader Chain - Combines multiple loaders
//=============================================================================

class LoaderChain : public ModuleLoader {
public:
    LoaderChain() = default;
    
    explicit LoaderChain(std::vector<ModuleLoaderPtr> loaders) 
        : loaders_(std::move(loaders)) {}
    
    void AddLoader(ModuleLoaderPtr loader) {
        loaders_.push_back(std::move(loader));
    }
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        for (const auto& loader : loaders_) {
            if (loader->CanLoad(resolved_path)) {
                auto result = loader->Load(resolved_path);
                if (result) {
                    LOG_DEBUG("LoaderChain", 
                        loader->GetName() + " loaded: " + resolved_path);
                    return result;
                }
            }
        }
        return std::nullopt;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        for (const auto& loader : loaders_) {
            if (loader->CanLoad(resolved_path)) return true;
        }
        return false;
    }
    
    std::string GetName() const override { return "LoaderChain"; }
    
    std::vector<std::string> GetLoaderNames() const {
        std::vector<std::string> names;
        for (const auto& loader : loaders_) {
            names.push_back(loader->GetName());
        }
        return names;
    }
    
private:
    std::vector<ModuleLoaderPtr> loaders_;
};

//=============================================================================
// Transforming Loader - Wraps a loader and applies transformations
//=============================================================================

class TransformingLoader : public ModuleLoader {
public:
    TransformingLoader(ModuleLoaderPtr inner_loader, ModuleTransformerPtr transformer)
        : inner_loader_(std::move(inner_loader))
        , transformer_(std::move(transformer)) {}
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        // First, load the module using the inner loader
        auto module_info = inner_loader_->Load(resolved_path);
        if (!module_info) return std::nullopt;
        
        // Check if we should transform this module
        if (transformer_ && transformer_->ShouldTransform(resolved_path)) {
            auto transform_result = transformer_->Transform(
                module_info->source, 
                resolved_path, 
                module_info->format
            );
            
            if (transform_result.is_ok()) {
                // Store original and apply transformation
                module_info->original_source = module_info->source;
                module_info->source = transform_result.code;
                module_info->transformed = true;
                
                // Store source map if available
                if (transform_result.source_map) {
                    module_info->source_map = transform_result.source_map;
                }
                
                // Update format if TypeScript was transformed to JavaScript
                if (resolved_path.ends_with(".ts") || 
                    resolved_path.ends_with(".tsx") ||
                    resolved_path.ends_with(".mts") ||
                    resolved_path.ends_with(".cts")) {
                    // After transformation, treat as JS
                    if (resolved_path.ends_with(".mts")) {
                        module_info->format = ModuleFormat::ESModule;
                    } else if (resolved_path.ends_with(".cts")) {
                        module_info->format = ModuleFormat::CommonJS;
                    }
                }
                
                LOG_DEBUG("TransformingLoader", "Transformed: " + resolved_path);
            } else {
                // Log transformation errors
                for (const auto& error : transform_result.errors) {
                    LOG_ERROR("TransformingLoader", "Transform error: " + error);
                }
                // Return original code on error (or could return nullopt)
            }
        }
        
        return module_info;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        return inner_loader_->CanLoad(resolved_path);
    }
    
    std::string GetName() const override { 
        return "TransformingLoader(" + inner_loader_->GetName() + ")"; 
    }
    
    // Set/replace transformer
    void SetTransformer(ModuleTransformerPtr transformer) {
        transformer_ = std::move(transformer);
    }
    
    // Add to transformer chain (creates chain if needed)
    void AddTransformer(ModuleTransformerPtr transformer) {
        if (!transformer_) {
            transformer_ = std::move(transformer);
        } else {
            // Check if already a chain
            auto chain = std::dynamic_pointer_cast<TransformerChain>(transformer_);
            if (chain) {
                chain->AddTransformer(std::move(transformer));
            } else {
                // Create a new chain with existing + new
                auto new_chain = std::make_shared<TransformerChain>();
                new_chain->AddTransformer(std::move(transformer_));
                new_chain->AddTransformer(std::move(transformer));
                transformer_ = new_chain;
            }
        }
    }
    
    // Configure source map behavior
    void SetInlineSourceMaps(bool inline_) { inline_source_maps_ = inline_; }
    
    // Get the inner loader
    ModuleLoaderPtr GetInnerLoader() const { return inner_loader_; }
    
private:
    ModuleLoaderPtr inner_loader_;
    ModuleTransformerPtr transformer_;
    bool inline_source_maps_{true};
};

//=============================================================================
// Module System - Orchestrates Resolvers and Loaders
//=============================================================================

class ModuleSystem {
public:
    ModuleSystem() = default;
    
    ModuleSystem(ModuleResolverPtr resolver, ModuleLoaderPtr loader)
        : resolver_(std::move(resolver)), loader_(std::move(loader)) {}
    
    // Set resolver (handles path resolution)
    void SetResolver(ModuleResolverPtr resolver) {
        resolver_ = std::move(resolver);
    }
    
    // Set loader (handles content loading)
    void SetLoader(ModuleLoaderPtr loader) {
        loader_ = std::move(loader);
    }
    
    // Resolve and load a module
    std::optional<ModuleInfo> Require(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) {
        if (!resolver_ || !loader_) {
            LOG_ERROR("ModuleSystem", "Resolver or loader not configured");
            return std::nullopt;
        }
        
        // Step 1: Resolve
        auto resolved = resolver_->Resolve(specifier, parent_path);
        if (!resolved) {
            LOG_DEBUG("ModuleSystem", "Could not resolve: " + specifier);
            return std::nullopt;
        }
        
        // Step 2: Load
        auto module_info = loader_->Load(resolved->resolved_path);
        if (!module_info) {
            LOG_DEBUG("ModuleSystem", "Could not load: " + resolved->resolved_path);
            return std::nullopt;
        }
        
        module_info->specifier = specifier;
        return module_info;
    }
    
    // Just resolve (without loading)
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) {
        if (!resolver_) return std::nullopt;
        return resolver_->Resolve(specifier, parent_path);
    }
    
    // Just load (from already-resolved path)
    std::optional<ModuleInfo> Load(const std::string& resolved_path) {
        if (!loader_) return std::nullopt;
        return loader_->Load(resolved_path);
    }
    
    ModuleResolverPtr GetResolver() const { return resolver_; }
    ModuleLoaderPtr GetLoader() const { return loader_; }
    
private:
    ModuleResolverPtr resolver_;
    ModuleLoaderPtr loader_;
};

//=============================================================================
// Module System Factory - Creates common configurations
//=============================================================================

class ModuleSystemFactory {
public:
    // Create standard module system (virtual -> disk)
    static std::shared_ptr<ModuleSystem> CreateStandard() {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create full module system (virtual -> npm -> jsr -> disk -> remote)
    static std::shared_ptr<ModuleSystem> CreateFull(
        const std::vector<std::string>& allowed_remote_prefixes = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<NpmResolver>());
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        auto remote_resolver = std::make_shared<RemoteResolver>();
        if (!allowed_remote_prefixes.empty()) {
            remote_resolver->SetAllowedPrefixes(allowed_remote_prefixes);
        }
        resolver_chain->AddResolver(remote_resolver);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create sandboxed module system (virtual only)
    static std::shared_ptr<ModuleSystem> CreateSandboxed() {
        return std::make_shared<ModuleSystem>(
            std::make_shared<VirtualResolver>(),
            std::make_shared<VirtualLoader>()
        );
    }
    
    // Create disk-only module system
    static std::shared_ptr<ModuleSystem> CreateDiskOnly(
        const std::vector<std::string>& search_paths = {"."}
    ) {
        return std::make_shared<ModuleSystem>(
            std::make_shared<DiskResolver>(search_paths),
            std::make_shared<DiskLoader>()
        );
    }
    
    // Create module system with npm: support
    static std::shared_ptr<ModuleSystem> CreateWithNpm(
        const std::string& node_modules_path = ""
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create module system with npm: and jsr: support
    static std::shared_ptr<ModuleSystem> CreateWithJsr(
        const std::string& node_modules_path = ""
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create module system with TypeScript transformation
    static std::shared_ptr<ModuleSystem> CreateWithTypeScript(
        const std::vector<std::string>& search_paths = {"."},
        TypeScriptTransformer::Options ts_options = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>(search_paths));
        
        // Create a transforming loader wrapping the disk loader
        auto disk_loader = std::make_shared<DiskLoader>();
        auto ts_transformer = std::make_shared<TypeScriptTransformer>(std::move(ts_options));
        auto transforming_loader = std::make_shared<TransformingLoader>(disk_loader, ts_transformer);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(transforming_loader);
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create full-featured module system with npm/jsr and TypeScript
    static std::shared_ptr<ModuleSystem> CreateTypedFull(
        const std::string& node_modules_path = "",
        TypeScriptTransformer::Options ts_options = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        // Create a transforming loader chain
        auto ts_transformer = std::make_shared<TypeScriptTransformer>(std::move(ts_options));
        
        auto disk_loader = std::make_shared<DiskLoader>();
        auto transforming_disk = std::make_shared<TransformingLoader>(disk_loader, ts_transformer);
        
        auto remote_loader = std::make_shared<RemoteLoader>();
        auto transforming_remote = std::make_shared<TransformingLoader>(remote_loader, ts_transformer);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(transforming_disk);
        loader_chain->AddLoader(transforming_remote);
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
};

// Backwards-compatible aliases
using ModuleLoaderFactory = ModuleSystemFactory;

//=============================================================================
// Script - Enhanced with timeout, priority, metrics
//=============================================================================

enum class ScriptState {
    Pending,
    Running,
    Completed,
    Failed,
    TimedOut,
    Cancelled
};

class Script : public std::enable_shared_from_this<Script> {
public:
    using CompletionCallback = std::function<void(bool success, const std::string& result, const ScriptError& error)>;
    using ConsoleCallback = std::function<void(const std::string& type, const std::string& message)>;
    
    struct Options {
        std::string name;
        ScriptPriority priority{ScriptPriority::Normal};
        std::chrono::milliseconds timeout{0};  // 0 = use environment default
        ScriptContextPtr context;
        
        // Script-level permissions (will be intersected with environment permissions)
        // If not set, uses environment defaults
        std::optional<PermissionSet> permissions;
        
        // Metadata for auditing/debugging
        std::string source_file;     // Where this script came from
        std::string author;          // Who wrote it
        bool trusted{false};         // Is this a trusted script?
    };
    
    explicit Script(std::string code, Options options = {})
        : code_(std::move(code))
        , name_(options.name.empty() ? GenerateId() : std::move(options.name))
        , priority_(options.priority)
        , timeout_(options.timeout)
        , context_(options.context)
        , permissions_(options.permissions)
        , source_file_(std::move(options.source_file))
        , author_(std::move(options.author))
        , trusted_(options.trusted) {}
    
    // Accessors
    std::string GetCode() const { std::shared_lock lock(mutex_); return code_; }
    std::string GetName() const { std::shared_lock lock(mutex_); return name_; }
    ScriptPriority GetPriority() const { return priority_; }
    std::chrono::milliseconds GetTimeout() const { return timeout_; }
    ScriptContextPtr GetContext() const { return context_; }
    
    // Permission accessors
    std::optional<PermissionSet> GetPermissions() const { return permissions_; }
    std::string GetSourceFile() const { return source_file_; }
    std::string GetAuthor() const { return author_; }
    bool IsTrusted() const { return trusted_; }
    
    // Check if script has a specific permission (requires environment context)
    bool HasPermission(ScriptPermission perm) const {
        if (!permissions_.has_value()) return true;  // Uses env default
        return permissions_->Has(perm);
    }
    
    ScriptState GetState() const { return state_.load(std::memory_order_acquire); }
    std::string GetResult() const { std::shared_lock lock(mutex_); return result_; }
    ScriptError GetError() const { std::shared_lock lock(mutex_); return error_; }
    
    ExecutionMetrics GetMetrics() const { std::shared_lock lock(mutex_); return metrics_; }
    
    bool IsComplete() const {
        auto state = GetState();
        return state == ScriptState::Completed || 
               state == ScriptState::Failed || 
               state == ScriptState::TimedOut ||
               state == ScriptState::Cancelled;
    }
    
    // Callbacks
    void OnComplete(CompletionCallback callback) {
        std::unique_lock lock(mutex_);
        completion_callback_ = std::move(callback);
        
        if (IsComplete() && completion_callback_) {
            auto cb = completion_callback_;
            auto success = state_.load() == ScriptState::Completed;
            auto result = result_;
            auto error = error_;
            lock.unlock();
            cb(success, result, error);
        }
    }
    
    void OnConsole(ConsoleCallback callback) {
        std::lock_guard lock(mutex_);
        console_callback_ = std::move(callback);
    }
    
    // Wait
    bool Wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::unique_lock lock(mutex_);
        auto predicate = [this] { return IsComplete(); };
        
        if (timeout == std::chrono::milliseconds::max()) {
            cv_.wait(lock, predicate);
            return true;
        }
        return cv_.wait_for(lock, timeout, predicate);
    }
    
    // Cancel
    void Cancel() { cancel_requested_.store(true, std::memory_order_release); }
    bool IsCancelRequested() const { return cancel_requested_.load(std::memory_order_acquire); }
    
private:
    friend class ScriptEnvironment;
    
    void SetState(ScriptState state) { state_.store(state, std::memory_order_release); }
    
    void Start() {
        std::unique_lock lock(mutex_);
        metrics_.start_time = std::chrono::steady_clock::now();
        state_.store(ScriptState::Running, std::memory_order_release);
    }
    
    void Complete(const std::string& result) {
        std::unique_lock lock(mutex_);
        result_ = result;
        metrics_.end_time = std::chrono::steady_clock::now();
        metrics_.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            metrics_.end_time - metrics_.start_time);
        state_.store(ScriptState::Completed, std::memory_order_release);
        
        auto cb = completion_callback_;
        lock.unlock();
        cv_.notify_all();
        if (cb) cb(true, result, ScriptError::None());
    }
    
    void Fail(const ScriptError& error) {
        std::unique_lock lock(mutex_);
        error_ = error;
        metrics_.end_time = std::chrono::steady_clock::now();
        metrics_.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            metrics_.end_time - metrics_.start_time);
        
        ScriptState newState = ScriptState::Failed;
        if (error.code == ErrorCode::Timeout) newState = ScriptState::TimedOut;
        if (error.code == ErrorCode::Cancelled) newState = ScriptState::Cancelled;
        state_.store(newState, std::memory_order_release);
        
        auto cb = completion_callback_;
        lock.unlock();
        cv_.notify_all();
        if (cb) cb(false, "", error);
    }
    
    void EmitConsole(const std::string& type, const std::string& message) {
        ConsoleCallback cb;
        { std::shared_lock lock(mutex_); cb = console_callback_; }
        if (cb) cb(type, message);
    }
    
    static std::string GenerateId() {
        static std::atomic<uint64_t> counter{0};
        return "script_" + std::to_string(++counter);
    }
    
    std::string code_;
    std::string name_;
    ScriptPriority priority_;
    std::chrono::milliseconds timeout_;
    ScriptContextPtr context_;
    
    std::atomic<ScriptState> state_{ScriptState::Pending};
    std::string result_;
    ScriptError error_;
    ExecutionMetrics metrics_;
    std::atomic<bool> cancel_requested_{false};
    
    // Permission fields
    std::optional<PermissionSet> permissions_;
    std::string source_file_;
    std::string author_;
    bool trusted_{false};
    
    mutable std::shared_mutex mutex_;
    std::condition_variable_any cv_;
    CompletionCallback completion_callback_;
    ConsoleCallback console_callback_;
};

using ScriptPtr = std::shared_ptr<Script>;

// Priority comparator for queue
struct ScriptPriorityCompare {
    bool operator()(const ScriptPtr& a, const ScriptPtr& b) const {
        return static_cast<int>(a->GetPriority()) < static_cast<int>(b->GetPriority());
    }
};

//=============================================================================
// ScriptEnvironment - Enhanced with config, metrics, graceful shutdown
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
    Result<std::string> ExecuteSync(const std::string& code, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    Result<std::string> ExecuteFile(const std::filesystem::path& path, std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    
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
    queue_cv_.notify_one();
    
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
            
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return !script_queue_.empty() || stop_requested_.load();
                });
            }
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
    
    std::atomic<bool> timed_out{false};
    std::atomic<bool> script_done{false};
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
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(isolate, script->GetCode().c_str()).ToLocal(&source)) {
        script->Fail(ScriptError::Make(ErrorCode::InternalError, "Failed to create source"));
        signalTimeoutDone();
        return;
    }
    
    v8::Local<v8::Script> compiled;
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
    v8::Local<v8::Value> result;
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

//=============================================================================
// ScriptEngine Implementation
//=============================================================================

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

//=============================================================================
// Test Main
//=============================================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

int main() {
    std::cout << "=== Enhanced ScriptEngine Test ===" << std::endl;
    
    std::vector<TestResult> results;
    
    auto& engine = ScriptEngine::Instance();
    
    // Setup logging (quieter for tests)
    engine.SetLogLevel(LogLevel::Warn);
    
    // Helper for timing
    auto now = []() { return std::chrono::steady_clock::now(); };
    auto ms_since = [](auto start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
    };
    
    // Initialize
    std::cout << "\n--- Initializing Engine ---" << std::endl;
    auto init_start = now();
    if (!engine.Initialize()) {
        std::cerr << "FATAL: Failed to initialize engine" << std::endl;
        return 1;
    }
    auto init_time = ms_since(init_start);
    std::cout << "  Init time: " << init_time << "ms" << std::endl;
    results.push_back({"Engine Init", true, std::to_string(init_time) + "ms"});
    
    // Test 1: Basic execution with timing
    std::cout << "\n--- Test 1: Basic Execution ---" << std::endl;
    {
        auto start = now();
        auto result = engine.ExecuteSync("40 + 2;");
        auto duration = ms_since(start);
        
        bool passed = result.IsOk() && result.Value() == "42";
        std::cout << "  Duration: " << duration << "ms" << std::endl;
        std::cout << (passed ? "[PASS]" : "[FAIL]") << " Result: " 
                  << (result.IsOk() ? result.Value() : result.Error().message) << std::endl;
        results.push_back({"Basic Execution", passed, 
            (result.IsOk() ? result.Value() : result.Error().message) + " (" + std::to_string(duration) + "ms)"});
    }
    
    // Test 2: Script timeout with detailed timing
    std::cout << "\n--- Test 2: Script Timeout ---" << std::endl;
    {
        const int64_t TIMEOUT_MS = 500;
        auto mainEnv = engine.GetMainEnvironment();
        std::cout << "  Env running: " << (mainEnv->IsRunning() ? "yes" : "no") << std::endl;
        
        auto script = engine.CreateScript(
            "let x = 0; while(true) { x++; }",
            {.name = "timeout-test", .timeout = std::chrono::milliseconds(TIMEOUT_MS)}
        );
        
        std::cout << "  Before Execute - State: " << static_cast<int>(script->GetState()) << std::endl;
        
        auto exec_start = now();
        bool executed = mainEnv->Execute(script);
        auto exec_time = ms_since(exec_start);
        std::cout << "  Execute time: " << exec_time << "ms, returned: " << (executed ? "true" : "false") << std::endl;
        
        // Small delay to ensure script starts
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "  After 50ms - State: " << static_cast<int>(script->GetState()) << std::endl;
        
        auto wait_start = now();
        script->Wait(std::chrono::seconds(10));
        auto wait_duration = ms_since(wait_start);
        
        std::cout << "  Wait duration: " << wait_duration << "ms (expected ~" << (TIMEOUT_MS - 50) << "ms)" << std::endl;
        std::cout << "  Final State: " << static_cast<int>(script->GetState()) << std::endl;
        std::cout << "  Error: " << script->GetError().message << std::endl;
        
        // Should be TimedOut and wait duration should be close to script timeout
        bool state_ok = script->GetState() == ScriptState::TimedOut;
        bool timing_ok = wait_duration < (TIMEOUT_MS + 200);  // Allow 200ms tolerance
        bool passed = state_ok && timing_ok;
        
        std::cout << (passed ? "[PASS]" : "[FAIL]") << " Timeout test" 
                  << " (state=" << (state_ok ? "ok" : "wrong") << ", timing=" << (timing_ok ? "ok" : "slow") << ")" << std::endl;
        results.push_back({"Script Timeout", passed, 
            "State=" + std::to_string(static_cast<int>(script->GetState())) + " " + std::to_string(wait_duration) + "ms"});
    }
    
    // Test 3: Priority Queue - Realistic scenario
    // Simulates: emergency stops, user actions, background processing, cleanup tasks
    std::cout << "\n--- Test 3: Priority Queue (Realistic Scenario) ---" << std::endl;
    {
        auto start = now();
        std::vector<std::string> execution_order;
        std::mutex order_mutex;
        
        // Simulate different priority tasks
        // CRITICAL: Emergency stop handler (must run first)
        auto emergency = engine.CreateScript(R"(
            // Emergency stop - highest priority
            globalThis.emergencyResult = 'EMERGENCY_HANDLED';
            'emergency_done';
        )", {.name = "emergency-stop", .priority = ScriptPriority::Critical});
        
        // HIGH: User interaction response (UI button click)
        auto userAction = engine.CreateScript(R"(
            // User clicked a button - needs quick response
            const result = { action: 'button_click', processed: true };
            JSON.stringify(result);
        )", {.name = "user-action", .priority = ScriptPriority::High});
        
        // NORMAL: Background data processing
        auto dataProcess = engine.CreateScript(R"(
            // Process some data in background
            let sum = 0;
            for (let i = 0; i < 1000; i++) sum += i;
            'data_processed:' + sum;
        )", {.name = "data-processor", .priority = ScriptPriority::Normal});
        
        // LOW: Cleanup and logging tasks
        auto cleanup1 = engine.CreateScript(R"(
            // Cleanup old cache entries
            'cleanup_cache_done';
        )", {.name = "cleanup-cache", .priority = ScriptPriority::Low});
        
        auto cleanup2 = engine.CreateScript(R"(
            // Log analytics event
            'analytics_logged';
        )", {.name = "log-analytics", .priority = ScriptPriority::Low});
        
        // Track completion order via callbacks
        auto trackOrder = [&](const std::string& name) {
            return [&, name](bool success, const std::string& result, const ScriptError&) {
                if (success) {
                    std::lock_guard lock(order_mutex);
                    execution_order.push_back(name);
                }
            };
        };
        
        emergency->OnComplete(trackOrder("emergency"));
        userAction->OnComplete(trackOrder("userAction"));
        dataProcess->OnComplete(trackOrder("dataProcess"));
        cleanup1->OnComplete(trackOrder("cleanup1"));
        cleanup2->OnComplete(trackOrder("cleanup2"));
        
        // Queue in REVERSE priority order (low first, critical last)
        // Priority queue should still execute in correct order
        std::cout << "  Queueing in reverse order (low->critical)..." << std::endl;
        engine.GetMainEnvironment()->Execute(cleanup1);     // Low
        engine.GetMainEnvironment()->Execute(cleanup2);     // Low
        engine.GetMainEnvironment()->Execute(dataProcess);  // Normal
        engine.GetMainEnvironment()->Execute(userAction);   // High
        engine.GetMainEnvironment()->Execute(emergency);    // Critical
        
        // Wait for all
        emergency->Wait(std::chrono::seconds(5));
        userAction->Wait(std::chrono::seconds(5));
        dataProcess->Wait(std::chrono::seconds(5));
        cleanup1->Wait(std::chrono::seconds(5));
        cleanup2->Wait(std::chrono::seconds(5));
        
        auto duration = ms_since(start);
        
        // Check all completed
        bool all_completed = 
            emergency->GetState() == ScriptState::Completed &&
            userAction->GetState() == ScriptState::Completed &&
            dataProcess->GetState() == ScriptState::Completed &&
            cleanup1->GetState() == ScriptState::Completed &&
            cleanup2->GetState() == ScriptState::Completed;
        
        // Check priority order (critical should be first)
        bool priority_ok = !execution_order.empty() && execution_order[0] == "emergency";
        
        std::cout << "  Duration: " << duration << "ms" << std::endl;
        std::cout << "  Execution order: ";
        for (size_t i = 0; i < execution_order.size(); i++) {
            if (i > 0) std::cout << " -> ";
            std::cout << execution_order[i];
        }
        std::cout << std::endl;
        std::cout << "  Results:" << std::endl;
        std::cout << "    Emergency: " << emergency->GetResult() << std::endl;
        std::cout << "    UserAction: " << userAction->GetResult() << std::endl;
        std::cout << "    DataProcess: " << dataProcess->GetResult() << std::endl;
        
        bool passed = all_completed && priority_ok;
        std::cout << (passed ? "[PASS]" : "[FAIL]") 
                  << " Priority execution (all=" << (all_completed ? "ok" : "fail") 
                  << ", order=" << (priority_ok ? "ok" : "wrong") << ")" << std::endl;
        results.push_back({"Priority Queue", passed, 
            "Order: " + (execution_order.empty() ? "none" : execution_order[0]) + "... (" + std::to_string(duration) + "ms)"});
    }
    
    // Test 4: Script Context - Multiple scenarios
    std::cout << "\n--- Test 4: Script Context (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 5;
        
        // Scenario 4.1: Basic CRUD operations
        std::cout << "  4.1 Basic CRUD..." << std::endl;
        {
            auto ctx = std::make_shared<ScriptContext>();
            
            // Create
            ctx->Set("user", "john");
            ctx->Set("age", "30");
            
            // Read
            bool create_ok = ctx->Get("user").value_or("") == "john" &&
                            ctx->Get("age").value_or("") == "30";
            
            // Update
            ctx->Set("age", "31");
            bool update_ok = ctx->Get("age").value_or("") == "31";
            
            // Delete
            ctx->Remove("user");
            bool delete_ok = !ctx->Get("user").has_value();
            
            // Non-existent
            bool missing_ok = !ctx->Get("nonexistent").has_value();
            
            bool passed = create_ok && update_ok && delete_ok && missing_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // Scenario 4.2: Concurrent read/write (simulated with threads)
        std::cout << "  4.2 Concurrent access..." << std::endl;
        {
            auto ctx = std::make_shared<ScriptContext>();
            std::atomic<int> success_count{0};
            std::vector<std::thread> threads;
            
            // Multiple writers
            for (int i = 0; i < 10; i++) {
                threads.emplace_back([ctx, i, &success_count]() {
                    std::string key = "thread_" + std::to_string(i);
                    ctx->Set(key, std::to_string(i * 10));
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (ctx->Get(key).has_value()) {
                        success_count++;
                    }
                });
            }
            
            // Multiple readers
            for (int i = 0; i < 5; i++) {
                threads.emplace_back([ctx]() {
                    for (int j = 0; j < 10; j++) {
                        ctx->Get("thread_" + std::to_string(j));
                    }
                });
            }
            
            for (auto& t : threads) t.join();
            
            bool passed = success_count.load() == 10;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " (" << success_count.load() << "/10 writes verified)" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // Scenario 4.3: Bulk operations
        std::cout << "  4.3 Bulk operations..." << std::endl;
        {
            auto ctx = std::make_shared<ScriptContext>();
            
            // Add 100 items
            for (int i = 0; i < 100; i++) {
                ctx->Set("item_" + std::to_string(i), "value_" + std::to_string(i));
            }
            
            // Verify random samples
            bool sample_ok = ctx->Get("item_0").value_or("") == "value_0" &&
                            ctx->Get("item_50").value_or("") == "value_50" &&
                            ctx->Get("item_99").value_or("") == "value_99";
            
            // Get all and verify count
            auto all = ctx->GetAll();
            bool count_ok = all.size() == 100;
            
            // Clear and verify empty
            ctx->Clear();
            bool clear_ok = ctx->GetAll().empty();
            
            bool passed = sample_ok && count_ok && clear_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " (100 items, clear worked)" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // Scenario 4.4: Config-like usage pattern
        std::cout << "  4.4 Config pattern..." << std::endl;
        {
            auto config = std::make_shared<ScriptContext>();
            
            // Set default config
            config->Set("log_level", "info");
            config->Set("max_connections", "100");
            config->Set("timeout_ms", "5000");
            config->Set("feature_flag_x", "true");
            
            // Read with defaults
            auto getConfig = [&](const std::string& key, const std::string& def) {
                return config->Get(key).value_or(def);
            };
            
            bool passed = 
                getConfig("log_level", "warn") == "info" &&
                getConfig("max_connections", "50") == "100" &&
                getConfig("missing_key", "default") == "default";
            
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // Scenario 4.5: Shared context between scripts (simulated)
        std::cout << "  4.5 Script data sharing..." << std::endl;
        {
            auto shared = std::make_shared<ScriptContext>();
            
            // Script 1 "writes" data
            shared->Set("script1_result", "processed_data");
            shared->Set("script1_status", "complete");
            
            // Script 2 "reads" script 1's data and adds its own
            auto s1_result = shared->Get("script1_result").value_or("");
            shared->Set("script2_used", s1_result);
            shared->Set("script2_status", "complete");
            
            // Script 3 "aggregates" all results
            auto s1_status = shared->Get("script1_status").value_or("");
            auto s2_status = shared->Get("script2_status").value_or("");
            bool workflow_complete = s1_status == "complete" && s2_status == "complete";
            
            bool passed = workflow_complete && 
                         shared->Get("script2_used").value_or("") == "processed_data";
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Context tests: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Script Context", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Test 5: Metrics
    std::cout << "\n--- Test 5: Metrics ---" << std::endl;
    {
        auto mainEnv = engine.GetMainEnvironment();
        auto metrics = mainEnv->GetMetrics();
        
        bool passed = metrics.execution.scripts_executed > 0 && metrics.memory.used_heap_size > 0;
        std::cout << "  Scripts executed: " << metrics.execution.scripts_executed << std::endl;
        std::cout << "  Scripts failed: " << metrics.execution.scripts_failed << std::endl;
        std::cout << "  Scripts timed out: " << metrics.execution.scripts_timed_out << std::endl;
        std::cout << "  Heap used: " << metrics.memory.used_heap_size / 1024 << " KB" << std::endl;
        std::cout << (passed ? "[PASS]" : "[FAIL]") << " Metrics collected" << std::endl;
        results.push_back({"Metrics", passed, std::to_string(metrics.execution.scripts_executed) + " scripts"});
    }
    
    // Test 6: Custom Environment - Multiple Scenarios
    std::cout << "\n--- Test 6: Custom Environment (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 4;
        
        // 6.1: Basic custom environment with bootstrap
        std::cout << "  6.1 Bootstrap script..." << std::endl;
        {
            EnvironmentConfig config;
            config.name = "Bootstrap-Test";
            config.bootstrap_script = "globalThis.API_VERSION = '2.0'; globalThis.DEBUG = true;";
            
            auto env = engine.CreateEnvironment(config);
            bool passed = false;
            if (env) {
                auto r1 = env->ExecuteSync("API_VERSION;", std::chrono::seconds(2));
                auto r2 = env->ExecuteSync("DEBUG;", std::chrono::seconds(2));
                passed = r1.IsOk() && r1.Value() == "2.0" && 
                         r2.IsOk() && r2.Value() == "true";
            }
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 6.2: Environment isolation (separate environments don't share state)
        std::cout << "  6.2 Environment isolation..." << std::endl;
        {
            EnvironmentConfig config1, config2;
            config1.name = "Isolated-1";
            config1.bootstrap_script = "globalThis.envId = 'env1';";
            config2.name = "Isolated-2";
            config2.bootstrap_script = "globalThis.envId = 'env2';";
            
            auto env1 = engine.CreateEnvironment(config1);
            auto env2 = engine.CreateEnvironment(config2);
            
            bool passed = false;
            if (env1 && env2) {
                auto r1 = env1->ExecuteSync("envId;", std::chrono::seconds(2));
                auto r2 = env2->ExecuteSync("envId;", std::chrono::seconds(2));
                // Each env should have its own value
                passed = r1.IsOk() && r1.Value() == "env1" &&
                         r2.IsOk() && r2.Value() == "env2";
            }
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 6.3: Multi-script execution in custom env
        std::cout << "  6.3 Multi-script execution..." << std::endl;
        {
            EnvironmentConfig config;
            config.name = "Multi-Script";
            config.bootstrap_script = "globalThis.counter = 0;";
            
            auto env = engine.CreateEnvironment(config);
            bool passed = false;
            if (env) {
                // Run multiple scripts that modify shared state
                env->ExecuteSync("counter++;", std::chrono::seconds(1));
                env->ExecuteSync("counter++;", std::chrono::seconds(1));
                env->ExecuteSync("counter++;", std::chrono::seconds(1));
                auto result = env->ExecuteSync("counter;", std::chrono::seconds(1));
                passed = result.IsOk() && result.Value() == "3";
            }
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 6.4: Environment config with default timeout
        std::cout << "  6.4 Default timeout config..." << std::endl;
        {
            EnvironmentConfig config;
            config.name = "Timeout-Config-Test";
            config.default_script_timeout = std::chrono::milliseconds(100);  // Very short
            
            auto env = engine.CreateEnvironment(config);
            bool passed = false;
            if (env) {
                // Execute a slow script - should timeout due to env default
                auto script = std::make_shared<Script>(
                    "let x = 0; while(true) { x++; }",  // Infinite loop
                    Script::Options{}  // No explicit timeout - uses env default
                );
                env->Execute(script);
                script->Wait(std::chrono::seconds(2));
                
                // Should timeout with the env's default (100ms)
                passed = script->GetState() == ScriptState::TimedOut;
                std::cout << "         State: " << static_cast<int>(script->GetState()) << std::endl;
            }
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Custom Environment: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Custom Environment", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Test 7: Error Handling - Multiple Scenarios
    std::cout << "\n--- Test 7: Error Handling (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 5;
        
        // 7.1: Runtime error (throw)
        std::cout << "  7.1 Runtime error (throw)..." << std::endl;
        {
            auto result = engine.ExecuteSync("throw new Error('test error');", std::chrono::seconds(2));
            bool passed = result.IsError() && 
                         result.Error().code == ErrorCode::RuntimeError &&
                         result.Error().message.find("test error") != std::string::npos;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " " << result.Error().message << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 7.2: Syntax error (compile error)
        std::cout << "  7.2 Syntax error..." << std::endl;
        {
            auto result = engine.ExecuteSync("function { invalid syntax", std::chrono::seconds(2));
            bool passed = result.IsError() && result.Error().code == ErrorCode::CompileError;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " " << result.Error().message.substr(0, 50) << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 7.3: Reference error (undefined variable)
        std::cout << "  7.3 Reference error..." << std::endl;
        {
            auto result = engine.ExecuteSync("undefinedVar.property;", std::chrono::seconds(2));
            bool passed = result.IsError() && result.Error().code == ErrorCode::RuntimeError;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " " << result.Error().message << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 7.4: Type error
        std::cout << "  7.4 Type error..." << std::endl;
        {
            auto result = engine.ExecuteSync("null.toString();", std::chrono::seconds(2));
            bool passed = result.IsError() && result.Error().code == ErrorCode::RuntimeError;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " " << result.Error().message << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 7.5: Async error (unhandled promise rejection - may or may not propagate)
        std::cout << "  7.5 Stack trace capture..." << std::endl;
        {
            auto result = engine.ExecuteSync(R"(
                function level3() { throw new Error('deep error'); }
                function level2() { level3(); }
                function level1() { level2(); }
                level1();
            )", std::chrono::seconds(2));
            // Check that we captured a stack trace
            bool passed = result.IsError() && 
                         !result.Error().stack.empty() &&
                         result.Error().stack.find("level") != std::string::npos;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " Stack captured: " << (result.Error().stack.empty() ? "no" : "yes") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Error Handling: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Error Handling", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Test 8: Module System (Resolvers + Loaders)
    std::cout << "\n--- Test 8: Module System (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 6;
        
        // 8.1: VirtualResolver + VirtualLoader
        std::cout << "  8.1 Virtual resolver/loader..." << std::endl;
        {
            auto resolver = std::make_shared<VirtualResolver>();
            auto loader = std::make_shared<VirtualLoader>();
            
            // Register in both
            resolver->Register("math-utils");
            loader->Register("math-utils", R"(
                module.exports = { add: (a, b) => a + b };
            )");
            
            resolver->Register("config");
            loader->Register("config", R"(
                module.exports = { version: '1.0.0' };
            )");
            
            // Test resolution
            auto resolved1 = resolver->Resolve("math-utils", "");
            auto resolved2 = resolver->Resolve("virtual:config", "");
            auto resolved3 = resolver->Resolve("nonexistent", "");
            
            bool resolve_ok = resolved1.has_value() && resolved2.has_value() && !resolved3.has_value();
            
            // Test loading
            auto loaded = loader->Load(resolved1->resolved_path);
            bool load_ok = loaded.has_value() && loaded->source.find("module.exports") != std::string::npos;
            
            // Test list
            auto modules = loader->ListModules();
            bool list_ok = modules.size() == 2;
            
            // Test unregister
            resolver->Unregister("config");
            loader->Unregister("config");
            bool unregister_ok = !resolver->Resolve("config", "").has_value();
            
            bool passed = resolve_ok && load_ok && list_ok && unregister_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " (resolve=" << (resolve_ok ? "ok" : "fail")
                      << ", load=" << (load_ok ? "ok" : "fail")
                      << ", list=" << (list_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 8.2: DiskResolver
        std::cout << "  8.2 Disk resolver..." << std::endl;
        {
            auto resolver = std::make_shared<DiskResolver>(std::vector<std::string>{".", "./node_modules"});
            
            // Test that it can handle relative/absolute paths
            bool can_handle_relative = resolver->CanHandle("./test.js");
            bool can_handle_absolute = resolver->CanHandle("/some/path.js");
            bool can_handle_bare = resolver->CanHandle("lodash");
            bool rejects_url = !resolver->CanHandle("https://example.com/module.js");
            bool rejects_virtual = !resolver->CanHandle("virtual:test");
            
            bool passed = can_handle_relative && can_handle_absolute && 
                         can_handle_bare && rejects_url && rejects_virtual;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") 
                      << " (handles: relative, absolute, bare; rejects: url, virtual)" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 8.3: ResolverChain priority
        std::cout << "  8.3 Resolver chain priority..." << std::endl;
        {
            auto chain = std::make_shared<ResolverChain>();
            
            // Virtual resolver (high priority - first)
            auto virtualResolver = std::make_shared<VirtualResolver>();
            virtualResolver->Register("my-module");
            
            // Disk resolver (lower priority)
            auto diskResolver = std::make_shared<DiskResolver>();
            
            chain->AddResolver(virtualResolver);
            chain->AddResolver(diskResolver);
            
            // Verify chain order
            auto names = chain->GetResolverNames();
            bool order_ok = names.size() == 2 && names[0] == "VirtualResolver" && names[1] == "DiskResolver";
            
            // Virtual should resolve first
            auto resolved = chain->Resolve("my-module", "");
            bool priority_ok = resolved.has_value() && resolved->resolved_path.starts_with("virtual:");
            
            // Can handle check
            bool can_handle = chain->CanHandle("my-module") && 
                             chain->CanHandle("./local.js") &&
                             !chain->CanHandle("https://remote.com/mod.js");
            
            bool passed = order_ok && priority_ok && can_handle;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (order=" << (order_ok ? "ok" : "wrong")
                      << ", priority=" << (priority_ok ? "ok" : "wrong") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 8.4: ModuleSystem integration
        std::cout << "  8.4 ModuleSystem integration..." << std::endl;
        {
            auto resolver = std::make_shared<VirtualResolver>();
            auto loader = std::make_shared<VirtualLoader>();
            
            resolver->Register("test-module");
            loader->Register("test-module", "module.exports = 42;");
            
            ModuleSystem system(resolver, loader);
            
            // Test Require (resolve + load)
            auto module_info = system.Require("test-module");
            bool require_ok = module_info.has_value() && 
                             module_info->source.find("42") != std::string::npos;
            
            // Test separate resolve/load
            auto resolved = system.Resolve("test-module");
            bool resolve_ok = resolved.has_value();
            
            auto loaded = system.Load(resolved->resolved_path);
            bool load_ok = loaded.has_value();
            
            bool passed = require_ok && resolve_ok && load_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (require=" << (require_ok ? "ok" : "fail")
                      << ", resolve=" << (resolve_ok ? "ok" : "fail")
                      << ", load=" << (load_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 8.5: Module format detection
        std::cout << "  8.5 Format detection..." << std::endl;
        {
            auto loader = std::make_shared<VirtualLoader>();
            
            // Register with different formats
            loader->Register("esm-module", "export default 42;", ModuleFormat::ESModule);
            loader->Register("cjs-module", "module.exports = 42;", ModuleFormat::CommonJS);
            
            auto esm = loader->Load("virtual:esm-module");
            auto cjs = loader->Load("virtual:cjs-module");
            
            bool esm_ok = esm.has_value() && esm->format == ModuleFormat::ESModule;
            bool cjs_ok = cjs.has_value() && cjs->format == ModuleFormat::CommonJS;
            
            bool passed = esm_ok && cjs_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (ESM=" << (esm_ok ? "ok" : "fail")
                      << ", CJS=" << (cjs_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 8.6: Factory methods
        std::cout << "  8.6 ModuleSystemFactory..." << std::endl;
        {
            // Standard system
            auto standard = ModuleSystemFactory::CreateStandard();
            bool standard_ok = standard->GetResolver() != nullptr && standard->GetLoader() != nullptr;
            
            // Full system (with remote)
            auto full = ModuleSystemFactory::CreateFull({"https://cdn.example.com/"});
            bool full_ok = full->GetResolver() != nullptr;
            
            // Sandboxed (virtual only)
            auto sandboxed = ModuleSystemFactory::CreateSandboxed();
            bool sandboxed_ok = sandboxed->GetResolver() != nullptr;
            
            // Disk only
            auto disk = ModuleSystemFactory::CreateDiskOnly({"./lib", "./vendor"});
            bool disk_ok = disk->GetResolver() != nullptr;
            
            bool passed = standard_ok && full_ok && sandboxed_ok && disk_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (all factories create valid systems)" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Module System: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Module System", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Test 9: Permissions System
    std::cout << "\n--- Test 9: Permissions System (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 6;
        
        // 9.1: Basic permission operations
        std::cout << "  9.1 Basic grant/revoke..." << std::endl;
        {
            PermissionSet perms;
            
            // Start empty
            bool start_empty = perms.IsEmpty();
            
            // Grant
            perms.Grant(ScriptPermission::FileRead);
            perms.Grant(ScriptPermission::NetHttp);
            bool has_file = perms.Has(ScriptPermission::FileRead);
            bool has_net = perms.Has(ScriptPermission::NetHttp);
            bool no_write = !perms.Has(ScriptPermission::FileWrite);
            
            // Revoke
            perms.Revoke(ScriptPermission::FileRead);
            bool revoked = !perms.Has(ScriptPermission::FileRead);
            bool still_net = perms.Has(ScriptPermission::NetHttp);
            
            bool passed = start_empty && has_file && has_net && no_write && revoked && still_net;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 9.2: Preset factories
        std::cout << "  9.2 Preset factories..." << std::endl;
        {
            auto safe = PermissionSet::Safe();
            auto standard = PermissionSet::Standard();
            auto trusted = PermissionSet::Trusted();
            auto full = PermissionSet::Full();
            
            // Safe has timers
            bool safe_ok = safe.Has(ScriptPermission::Timers) && !safe.Has(ScriptPermission::FileRead);
            
            // Standard has modules
            bool standard_ok = standard.Has(ScriptPermission::ModuleRequire);
            
            // Trusted has OBS access
            bool trusted_ok = trusted.Has(ScriptPermission::ObsScenes) && trusted.Has(ScriptPermission::NetHttp);
            
            // Full has everything
            bool full_ok = full.Has(ScriptPermission::Full) && full.Has(ScriptPermission::ProcessSpawn);
            
            bool passed = safe_ok && standard_ok && trusted_ok && full_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 9.3: Bitwise operations
        std::cout << "  9.3 Bitwise operations..." << std::endl;
        {
            // Combine permissions
            auto combined = ScriptPermission::FileRead | ScriptPermission::FileWrite;
            PermissionSet perms(combined);
            bool has_both = perms.Has(ScriptPermission::FileRead) && perms.Has(ScriptPermission::FileWrite);
            
            // HasAny
            bool has_any = perms.HasAny(ScriptPermission::FileSystem);
            bool no_net = !perms.HasAny(ScriptPermission::Network);
            
            bool passed = has_both && has_any && no_net;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 9.4: Permission merging
        std::cout << "  9.4 Permission merging..." << std::endl;
        {
            PermissionSet a(ScriptPermission::FileRead);
            PermissionSet b(ScriptPermission::NetHttp);
            
            // Merge (union)
            PermissionSet merged = a;
            merged.Merge(b);
            bool merge_ok = merged.Has(ScriptPermission::FileRead) && merged.Has(ScriptPermission::NetHttp);
            
            // Intersect
            PermissionSet full(ScriptPermission::Full);
            PermissionSet limited(ScriptPermission::FileRead | ScriptPermission::Timers);
            full.Intersect(limited);
            bool intersect_ok = full.Has(ScriptPermission::FileRead) && 
                               full.Has(ScriptPermission::Timers) &&
                               !full.Has(ScriptPermission::NetHttp);
            
            bool passed = merge_ok && intersect_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 9.5: EnvironmentConfig presets
        std::cout << "  9.5 EnvironmentConfig presets..." << std::endl;
        {
            auto sandboxed = EnvironmentConfig::Sandboxed();
            auto trusted = EnvironmentConfig::Trusted();
            auto default_cfg = EnvironmentConfig::Default();
            
            bool sandboxed_ok = sandboxed.name == "Sandboxed" && 
                               !sandboxed.allow_file_access &&
                               sandboxed.permissions.Has(ScriptPermission::Timers);
            
            bool trusted_ok = trusted.permissions.Has(ScriptPermission::ObsScenes);
            
            bool default_ok = default_cfg.permissions.Has(ScriptPermission::ModuleRequire);
            
            bool passed = sandboxed_ok && trusted_ok && default_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 9.6: Script-level permissions
        std::cout << "  9.6 Script-level permissions..." << std::endl;
        {
            // Script with explicit permissions
            Script::Options opts;
            opts.name = "sandboxed-script";
            opts.permissions = PermissionSet::Safe();
            opts.source_file = "user-scripts/untrusted.js";
            opts.author = "unknown";
            opts.trusted = false;
            
            auto script = std::make_shared<Script>("'test';", opts);
            
            bool perms_ok = script->HasPermission(ScriptPermission::Timers) &&
                           !script->HasPermission(ScriptPermission::FileRead);
            bool meta_ok = script->GetSourceFile() == "user-scripts/untrusted.js" &&
                          script->GetAuthor() == "unknown" &&
                          !script->IsTrusted();
            
            // Script without permissions (uses env default)
            auto default_script = std::make_shared<Script>("'test';", Script::Options{});
            bool default_ok = default_script->HasPermission(ScriptPermission::FileRead);  // true = uses env default
            
            bool passed = perms_ok && meta_ok && default_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Permissions: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Permissions", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Test 10: NPM/JSR Resolvers and Transformers
    std::cout << "\n--- Test 10: Package Resolvers & Transformers (Multiple Scenarios) ---" << std::endl;
    {
        int scenarios_passed = 0;
        int total_scenarios = 6;
        
        // 10.1: NPM package spec parsing
        std::cout << "  10.1 NPM package spec parsing..." << std::endl;
        {
            // Simple package
            auto simple = NpmPackageSpec::Parse("npm:lodash");
            bool simple_ok = simple && simple->name == "lodash" && simple->version == "latest";
            
            // With version
            auto versioned = NpmPackageSpec::Parse("npm:react@18.2.0");
            bool versioned_ok = versioned && versioned->name == "react" && versioned->version == "18.2.0";
            
            // Scoped package
            auto scoped = NpmPackageSpec::Parse("npm:@types/node@20.0.0");
            bool scoped_ok = scoped && scoped->name == "@types/node" && scoped->version == "20.0.0";
            
            // With subpath
            auto subpath = NpmPackageSpec::Parse("npm:lodash@4.17.21/cloneDeep");
            bool subpath_ok = subpath && subpath->name == "lodash" && 
                             subpath->version == "4.17.21" && subpath->subpath == "/cloneDeep";
            
            // Invalid
            auto invalid = NpmPackageSpec::Parse("http://example.com");
            bool invalid_ok = !invalid.has_value();
            
            bool passed = simple_ok && versioned_ok && scoped_ok && subpath_ok && invalid_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (simple=" << (simple_ok ? "ok" : "fail")
                      << ", versioned=" << (versioned_ok ? "ok" : "fail")
                      << ", scoped=" << (scoped_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 10.2: JSR package spec parsing
        std::cout << "  10.2 JSR package spec parsing..." << std::endl;
        {
            // Standard JSR package
            auto std = JsrPackageSpec::Parse("jsr:@std/path@1.0.0");
            bool std_ok = std && std->scope == "@std" && std->name == "path" && std->version == "1.0.0";
            
            // With subpath
            auto subpath = JsrPackageSpec::Parse("jsr:@std/fs@0.5.0/walk");
            bool subpath_ok = subpath && subpath->name == "fs" && subpath->subpath == "/walk";
            
            // No version
            auto noversion = JsrPackageSpec::Parse("jsr:@oak/oak");
            bool noversion_ok = noversion && noversion->name == "oak" && noversion->version.empty();
            
            // Invalid (not scoped)
            auto invalid = JsrPackageSpec::Parse("jsr:lodash");
            bool invalid_ok = !invalid.has_value();
            
            bool passed = std_ok && subpath_ok && noversion_ok && invalid_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (std=" << (std_ok ? "ok" : "fail")
                      << ", subpath=" << (subpath_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 10.3: NPM Resolver
        std::cout << "  10.3 NPM resolver..." << std::endl;
        {
            auto resolver = std::make_shared<NpmResolver>();
            
            // Can handle npm: URLs
            bool can_handle = resolver->CanHandle("npm:lodash") && 
                             resolver->CanHandle("npm:@types/node@20.0.0") &&
                             !resolver->CanHandle("./local.js") &&
                             !resolver->CanHandle("jsr:@std/path");
            
            // Resolve to CDN URL
            auto resolved = resolver->Resolve("npm:react@18.2.0", "");
            bool resolve_ok = resolved && resolved->resolved_path.find("esm.sh") != std::string::npos;
            
            bool passed = can_handle && resolve_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (can_handle=" << (can_handle ? "ok" : "fail")
                      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 10.4: JSR Resolver
        std::cout << "  10.4 JSR resolver..." << std::endl;
        {
            auto resolver = std::make_shared<JsrResolver>();
            
            // Can handle jsr: URLs
            bool can_handle = resolver->CanHandle("jsr:@std/path") && 
                             !resolver->CanHandle("npm:lodash") &&
                             !resolver->CanHandle("./local.js");
            
            // Resolve to JSR URL
            auto resolved = resolver->Resolve("jsr:@std/path@1.0.0/mod.ts", "");
            bool resolve_ok = resolved && resolved->resolved_path.find("jsr.io") != std::string::npos;
            
            bool passed = can_handle && resolve_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (can_handle=" << (can_handle ? "ok" : "fail")
                      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 10.5: TypeScript Transformer
        std::cout << "  10.5 TypeScript transformer..." << std::endl;
        {
            TypeScriptTransformer::Options opts;
            opts.generate_source_map = true;
            auto transformer = std::make_shared<TypeScriptTransformer>(opts);
            
            // Should transform TypeScript files
            bool should_ts = transformer->ShouldTransform("foo.ts");
            bool should_tsx = transformer->ShouldTransform("component.tsx");
            bool should_mts = transformer->ShouldTransform("module.mts");
            bool should_not_js = !transformer->ShouldTransform("script.js");
            
            // Transform some TypeScript
            std::string ts_code = "const x: number = 42; export default x;";
            auto result = transformer->Transform(ts_code, "test.ts", ModuleFormat::ESModule);
            
            bool transform_ok = result.is_ok() && !result.code.empty();
            bool source_map_ok = result.source_map.has_value();
            
            bool passed = should_ts && should_tsx && should_mts && should_not_js && 
                         transform_ok && source_map_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (transform=" << (transform_ok ? "ok" : "fail")
                      << ", sourcemap=" << (source_map_ok ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        // 10.6: TransformingLoader
        std::cout << "  10.6 TransformingLoader..." << std::endl;
        {
            // Create a virtual loader with TypeScript content
            auto inner_loader = std::make_shared<VirtualLoader>();
            inner_loader->Register("app.ts", "const msg: string = 'hello'; console.log(msg);");
            inner_loader->Register("app.js", "const msg = 'hello'; console.log(msg);");
            
            auto transformer = std::make_shared<TypeScriptTransformer>();
            auto transforming_loader = std::make_shared<TransformingLoader>(inner_loader, transformer);
            
            // Load TypeScript file (should transform)
            auto ts_module = transforming_loader->Load("virtual:app.ts");
            bool ts_transformed = ts_module && ts_module->transformed;
            
            // Load JavaScript file (should NOT transform)
            auto js_module = transforming_loader->Load("virtual:app.js");
            bool js_not_transformed = js_module && !js_module->transformed;
            
            // Check name
            bool name_ok = transforming_loader->GetName().find("VirtualLoader") != std::string::npos;
            
            bool passed = ts_transformed && js_not_transformed && name_ok;
            std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
                      << " (ts_transform=" << (ts_transformed ? "ok" : "fail")
                      << ", js_passthrough=" << (js_not_transformed ? "ok" : "fail") << ")" << std::endl;
            if (passed) scenarios_passed++;
        }
        
        bool all_passed = scenarios_passed == total_scenarios;
        std::cout << (all_passed ? "[PASS]" : "[FAIL]") 
                  << " Package Resolvers & Transformers: " << scenarios_passed << "/" << total_scenarios << std::endl;
        results.push_back({"Pkg Resolvers/Transform", all_passed, 
            std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }
    
    // Shutdown with timing
    std::cout << "\n--- Shutdown ---" << std::endl;
    {
        auto start = now();
        engine.Shutdown(true, std::chrono::seconds(3));
        auto duration = ms_since(start);
        std::cout << "  Shutdown time: " << duration << "ms" << std::endl;
        results.push_back({"Graceful Shutdown", true, std::to_string(duration) + "ms"});
    }
    
    // Print Report
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║                        TEST REPORT                             ║" << std::endl;
    std::cout << "╠════════════════════════════════════════════════════════════════╣" << std::endl;
    
    int passed_count = 0, failed_count = 0;
    for (const auto& r : results) {
        if (r.passed) passed_count++; else failed_count++;
        std::cout << "║ " << (r.passed ? "✓ PASS" : "✗ FAIL") << " │ " 
                  << std::left << std::setw(20) << r.name 
                  << " │ " << std::setw(28) << r.message.substr(0, 28) << " ║" << std::endl;
    }
    
    std::cout << "╠════════════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ TOTAL: " << results.size() << " tests │ PASSED: " << passed_count 
              << " │ FAILED: " << failed_count << std::setw(24) << " ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
    
    return failed_count > 0 ? 1 : 0;
}

