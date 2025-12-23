#pragma once

/**
 * audit.hpp - Audit Logging System
 */

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

namespace experiments {

enum class AuditEventType {
    // Script lifecycle
    ScriptStart,
    ScriptEnd,
    ScriptError,
    ScriptTimeout,
    
    // Module system
    ModuleLoad,
    ModuleResolve,
    ModuleCompile,
    ModuleCacheHit,
    
    // File system
    FileRead,
    FileWrite,
    FileDelete,
    DirectoryRead,
    DirectoryCreate,
    
    // Network
    NetworkConnect,
    NetworkListen,
    HttpRequest,
    HttpResponse,
    WebSocketOpen,
    WebSocketClose,
    
    // Process
    ProcessSpawn,
    ProcessExit,
    EnvAccess,
    
    // Security
    PermissionDenied,
    SandboxViolation,
    ResourceLimitExceeded,
    
    // API calls
    ApiCall,
    NativeCall,
    EvalCall,
    
    // Workers
    WorkerCreate,
    WorkerTerminate,
    WorkerMessage
};

struct AuditEntry {
    AuditEventType type;
    std::chrono::system_clock::time_point timestamp;
    std::string script_id;
    std::string environment_id;
    std::string details;
    std::string source_file;
    int source_line{0};
    bool success{true};
    std::string error_message;
    std::unordered_map<std::string, std::string> metadata;
    
    std::string ToString() const {
        std::ostringstream oss;
        auto time = std::chrono::system_clock::to_time_t(timestamp);
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        oss << " [" << EventTypeName(type) << "] ";
        oss << (success ? "OK" : "FAIL") << " ";
        oss << details;
        if (!error_message.empty()) {
            oss << " ERROR: " << error_message;
        }
        return oss.str();
    }
    
    static std::string EventTypeName(AuditEventType type) {
        switch (type) {
            case AuditEventType::ScriptStart: return "SCRIPT_START";
            case AuditEventType::ScriptEnd: return "SCRIPT_END";
            case AuditEventType::ScriptError: return "SCRIPT_ERROR";
            case AuditEventType::ModuleLoad: return "MODULE_LOAD";
            case AuditEventType::FileRead: return "FILE_READ";
            case AuditEventType::FileWrite: return "FILE_WRITE";
            case AuditEventType::NetworkConnect: return "NET_CONNECT";
            case AuditEventType::HttpRequest: return "HTTP_REQUEST";
            case AuditEventType::PermissionDenied: return "PERM_DENIED";
            case AuditEventType::SandboxViolation: return "SANDBOX_VIOLATION";
            case AuditEventType::ResourceLimitExceeded: return "LIMIT_EXCEEDED";
            case AuditEventType::ApiCall: return "API_CALL";
            case AuditEventType::WorkerCreate: return "WORKER_CREATE";
            case AuditEventType::WorkerMessage: return "WORKER_MSG";
            default: return "UNKNOWN";
        }
    }
};

// Audit sink interface
class AuditSink {
public:
    virtual ~AuditSink() = default;
    virtual void Write(const AuditEntry& entry) = 0;
    virtual void Flush() = 0;
};

using AuditSinkPtr = std::shared_ptr<AuditSink>;

// Console sink
class ConsoleAuditSink : public AuditSink {
public:
    explicit ConsoleAuditSink(bool verbose = false) : verbose_(verbose) {}
    
    void Write(const AuditEntry& entry) override {
        if (!verbose_ && entry.success) return;
        std::cout << "[AUDIT] " << entry.ToString() << std::endl;
    }
    
    void Flush() override {
        std::cout.flush();
    }
    
private:
    bool verbose_;
};

// File sink
class FileAuditSink : public AuditSink {
public:
    explicit FileAuditSink(const std::string& path) {
        file_.open(path, std::ios::app);
    }
    
    ~FileAuditSink() {
        if (file_.is_open()) file_.close();
    }
    
    void Write(const AuditEntry& entry) override {
        if (file_.is_open()) {
            file_ << entry.ToString() << "\n";
        }
    }
    
    void Flush() override {
        if (file_.is_open()) file_.flush();
    }
    
private:
    std::ofstream file_;
};

// Callback sink
class CallbackAuditSink : public AuditSink {
public:
    using Callback = std::function<void(const AuditEntry&)>;
    
    explicit CallbackAuditSink(Callback callback) : callback_(std::move(callback)) {}
    
    void Write(const AuditEntry& entry) override {
        if (callback_) callback_(entry);
    }
    
    void Flush() override {}
    
private:
    Callback callback_;
};

// Main audit logger
class AuditLogger {
public:
    struct Options {
        bool enabled{true};
        bool log_successful{false};
        bool log_module_loads{true};
        bool log_file_access{true};
        bool log_network{true};
        bool log_api_calls{false};
        size_t max_entries{10000};
        bool async_write{true};
    };
    
    explicit AuditLogger(Options options = {}) : options_(std::move(options)) {}
    
    void AddSink(AuditSinkPtr sink) {
        std::lock_guard lock(mutex_);
        sinks_.push_back(std::move(sink));
    }
    
    void Log(AuditEventType type, const std::string& details,
             bool success = true, const std::string& error = "") {
        if (!options_.enabled) return;
        if (success && !options_.log_successful) return;
        
        AuditEntry entry;
        entry.type = type;
        entry.timestamp = std::chrono::system_clock::now();
        entry.details = details;
        entry.success = success;
        entry.error_message = error;
        
        LogEntry(std::move(entry));
    }
    
    void LogEntry(AuditEntry entry) {
        if (!options_.enabled) return;
        
        std::lock_guard lock(mutex_);
        
        for (auto& sink : sinks_) {
            sink->Write(entry);
        }
        
        if (entries_.size() >= options_.max_entries) {
            entries_.pop_front();
        }
        entries_.push_back(std::move(entry));
    }
    
    void LogModuleLoad(const std::string& specifier, const std::string& resolved) {
        if (!options_.log_module_loads) return;
        Log(AuditEventType::ModuleLoad, specifier + " -> " + resolved);
    }
    
    void LogFileAccess(AuditEventType type, const std::string& path, bool success,
                       const std::string& error = "") {
        if (!options_.log_file_access) return;
        Log(type, path, success, error);
    }
    
    void LogNetworkAccess(AuditEventType type, const std::string& url, bool success,
                          const std::string& error = "") {
        if (!options_.log_network) return;
        Log(type, url, success, error);
    }
    
    void LogPermissionDenied(const std::string& permission, const std::string& resource) {
        Log(AuditEventType::PermissionDenied, permission + ": " + resource, false);
    }
    
    void LogSandboxViolation(const std::string& violation) {
        Log(AuditEventType::SandboxViolation, violation, false);
    }
    
    std::vector<AuditEntry> GetEntries(size_t limit = 100) const {
        std::lock_guard lock(mutex_);
        std::vector<AuditEntry> result;
        size_t count = std::min(limit, entries_.size());
        auto it = entries_.rbegin();
        for (size_t i = 0; i < count; ++i, ++it) {
            result.push_back(*it);
        }
        return result;
    }
    
    void Flush() {
        std::lock_guard lock(mutex_);
        for (auto& sink : sinks_) {
            sink->Flush();
        }
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }
    
    Options& GetOptions() { return options_; }
    
private:
    Options options_;
    mutable std::mutex mutex_;
    std::vector<AuditSinkPtr> sinks_;
    std::deque<AuditEntry> entries_;
};

} // namespace experiments
