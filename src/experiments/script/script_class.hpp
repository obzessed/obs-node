#pragma once

/**
 * script_class.hpp - Script Class Definition
 * 
 * A script represents a unit of JavaScript code to be executed.
 * Includes priority, timeout, metrics, and permission support.
 */

#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <optional>
#include <atomic>
#include <shared_mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <random>

#include "../core/error.hpp"
#include "../core/metrics.hpp"
#include "../config/permissions.hpp"
#include "../config/script_context.hpp"
#include "script_result.hpp"

namespace experiments {

// Forward declarations
class ScriptEnvironment;

//=============================================================================
// Script State Machine
//=============================================================================

enum class ScriptState {
    Pending,
    Running,
    Completed,
    Failed,
    TimedOut,
    Cancelled
};

inline std::string ScriptStateToString(ScriptState state) {
    switch (state) {
        case ScriptState::Pending: return "pending";
        case ScriptState::Running: return "running";
        case ScriptState::Completed: return "completed";
        case ScriptState::Failed: return "failed";
        case ScriptState::TimedOut: return "timed_out";
        case ScriptState::Cancelled: return "cancelled";
    }
    return "unknown";
}

//=============================================================================
// Script - Enhanced with timeout, priority, metrics
//=============================================================================


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

    // Metadata accessors
    void SetMetadata(const std::string& key, const std::string& value) {
        std::unique_lock lock(mutex_);
        metadata_[key] = value;
    }
    
    std::optional<std::string> GetMetadata(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = metadata_.find(key);
        if (it != metadata_.end()) return it->second;
        return std::nullopt;
    }
    
    bool HasMetadata(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return metadata_.find(key) != metadata_.end();
    }
    
    std::unordered_map<std::string, std::string> GetAllMetadata() const {
        std::shared_lock lock(mutex_);
        return metadata_;
    }
    
    void ClearMetadata() {
        std::unique_lock lock(mutex_);
        metadata_.clear();
    }

    // Dependencies tracking
    void AddDependency(const std::string& module_name) {
        std::unique_lock lock(mutex_);
        dependencies_.insert(module_name);
    }
    
    std::vector<std::string> GetDependencies() const {
        std::shared_lock lock(mutex_);
        return std::vector<std::string>(dependencies_.begin(), dependencies_.end());
    }
    
    bool HasDependency(const std::string& module_name) const {
        std::shared_lock lock(mutex_);
        return dependencies_.find(module_name) != dependencies_.end();
    }
    
    void ClearDependencies() {
        std::unique_lock lock(mutex_);
        dependencies_.clear();
    }
    
    size_t DependencyCount() const {
        std::shared_lock lock(mutex_);
        return dependencies_.size();
    }

    // Check if script has a specific permission (requires environment context)
    bool HasPermission(ScriptPermission perm) const {
        if (!permissions_.has_value()) return true;  // Uses env default
        return permissions_->Has(perm);
    }

    ScriptState GetState() const { return state_.load(std::memory_order_acquire); }
    std::string GetResult() const { std::shared_lock lock(mutex_); return result_; }
    ScriptError GetError() const { std::shared_lock lock(mutex_); return error_; }
    
    // Get the raw V8 result value (if available)
    ScriptResult& GetResultValue() { std::shared_lock lock(mutex_); return result_value_; }
    const ScriptResult& GetResultValue() const { std::shared_lock lock(mutex_); return result_value_; }
    bool HasResultValue() const { std::shared_lock lock(mutex_); return result_value_.HasValue(); }

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
    
    // Set the raw V8 result value (called by ScriptEnvironment)
    void SetResultValue(ScriptResult&& value) {
        std::unique_lock lock(mutex_);
        result_value_ = std::move(value);
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
        // Generate UUID-like ID: 8-4-4-4-12 hex format
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;
        
        auto hex = [](uint64_t val, size_t len) {
            static const char* hexchars = "0123456789abcdef";
            std::string result;
            result.reserve(len);
            for (size_t i = 0; i < len; i++) {
                result += hexchars[(val >> ((len - 1 - i) * 4)) & 0xF];
            }
            return result;
        };
        
        uint64_t a = dist(gen);
        uint64_t b = dist(gen);
        return hex(a >> 32, 8) + "-" + hex(a >> 16, 4) + "-" + 
               hex(a, 4) + "-" + hex(b >> 48, 4) + "-" + hex(b, 12);
    }

    std::string code_;
    std::string name_;
    ScriptPriority priority_;
    std::chrono::milliseconds timeout_;
    ScriptContextPtr context_;

    std::atomic<ScriptState> state_{ScriptState::Pending};
    std::string result_;
    ScriptResult result_value_;  // Raw V8 value wrapper
    ScriptError error_;
    ExecutionMetrics metrics_;
    std::atomic<bool> cancel_requested_{false};

    // Permission fields
    std::optional<PermissionSet> permissions_;
    std::string source_file_;
    std::string author_;
    bool trusted_{false};

    // Metadata storage
    std::unordered_map<std::string, std::string> metadata_;

    // Dependencies storage
    std::unordered_set<std::string> dependencies_;

    mutable std::shared_mutex mutex_;
    std::condition_variable_any cv_;
    CompletionCallback completion_callback_;
    ConsoleCallback console_callback_;
};

using ScriptPtr = std::shared_ptr<Script>;

// Priority comparator for queue
// std::priority_queue is a max-heap: element with highest priority is at top
// Comparator returns true if 'a' has LOWER priority than 'b' (a should come after b)
// With Critical=3 > High=2 > Normal=1 > Low=0, we use a < b
struct ScriptPriorityCompare {
    bool operator()(const ScriptPtr& a, const ScriptPtr& b) const {
        return static_cast<int>(a->GetPriority()) < static_cast<int>(b->GetPriority());
    }
};

} // namespace experiments
