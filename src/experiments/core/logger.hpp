#pragma once

/**
 * logger.hpp - Enhanced Logging System with Async and JSON support
 */

#include <iostream>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace experiments {

//=============================================================================
// Log Levels and Formats
//=============================================================================

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

enum class LogFormat {
    Text,   // [LEVEL] [category] message
    Json,   // {"level": "...", "category": "...", "message": "...", "timestamp": "..."}
    Custom  // User-defined via callback
};

inline const char* LogLevelToString(LogLevel level) {
    static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    return names[static_cast<int>(level)];
}

//=============================================================================
// Log Entry
//=============================================================================

struct LogEntry {
    LogLevel level;
    std::string category;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    
    LogEntry() = default;
    LogEntry(LogLevel lvl, std::string cat, std::string msg)
        : level(lvl), category(std::move(cat)), message(std::move(msg)),
          timestamp(std::chrono::system_clock::now()) {}
};

//=============================================================================
// Log Formatters
//=============================================================================

class LogFormatter {
public:
    virtual ~LogFormatter() = default;
    virtual std::string Format(const LogEntry& entry) const = 0;
};

class TextLogFormatter : public LogFormatter {
public:
    std::string Format(const LogEntry& entry) const override {
        std::ostringstream ss;
        ss << "[" << LogLevelToString(entry.level) << "] "
           << "[" << entry.category << "] " << entry.message;
        return ss.str();
    }
};

class JsonLogFormatter : public LogFormatter {
public:
    std::string Format(const LogEntry& entry) const override {
        auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
        std::ostringstream ss;
        ss << "{\"level\":\"" << LogLevelToString(entry.level) << "\","
           << "\"category\":\"" << EscapeJson(entry.category) << "\","
           << "\"message\":\"" << EscapeJson(entry.message) << "\","
           << "\"timestamp\":\"" << std::put_time(std::gmtime(&time), "%FT%TZ") << "\"}";
        return ss.str();
    }
    
private:
    static std::string EscapeJson(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
};

//=============================================================================
// Synchronous Logger
//=============================================================================

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
    LogLevel GetMinLevel() const { return min_level_; }
    
    void SetFormat(LogFormat format) { format_ = format; }
    LogFormat GetFormat() const { return format_; }

    void Log(LogLevel level, const std::string& category, const std::string& message) {
        if (level < min_level_) return;

        LogEntry entry(level, category, message);
        
        std::lock_guard lock(mutex_);
        if (callback_) {
            callback_(level, category, message);
        } else {
            std::string formatted;
            if (format_ == LogFormat::Json) {
                formatted = json_formatter_.Format(entry);
            } else {
                formatted = text_formatter_.Format(entry);
            }
            std::cout << formatted << std::endl;
        }
    }

private:
    Logger() = default;
    std::mutex mutex_;
    LogCallback callback_;
    std::atomic<LogLevel> min_level_{LogLevel::Info};
    std::atomic<LogFormat> format_{LogFormat::Text};
    TextLogFormatter text_formatter_;
    JsonLogFormatter json_formatter_;
};

//=============================================================================
// Async Logger - Queue-based background logging
//=============================================================================

class AsyncLogger {
public:
    using LogCallback = std::function<void(const LogEntry&)>;
    
    explicit AsyncLogger(size_t max_queue_size = 1000)
        : max_queue_size_(max_queue_size), running_(false) {}
    
    ~AsyncLogger() { Stop(); }
    
    void Start() {
        if (running_) return;
        running_ = true;
        worker_ = std::thread([this] { ProcessLoop(); });
    }
    
    void Stop() {
        if (!running_) return;
        running_ = false;
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    
    bool IsRunning() const { return running_; }
    
    void SetCallback(LogCallback callback) {
        std::lock_guard lock(mutex_);
        callback_ = std::move(callback);
    }
    
    void SetMinLevel(LogLevel level) { min_level_ = level; }
    
    void Log(LogLevel level, const std::string& category, const std::string& message) {
        if (level < min_level_ || !running_) return;
        
        std::unique_lock lock(mutex_);
        if (queue_.size() >= max_queue_size_) {
            // Drop oldest if full
            queue_.pop();
        }
        queue_.emplace(level, category, message);
        lock.unlock();
        cv_.notify_one();
    }
    
    size_t GetPendingCount() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }
    
    void Flush() {
        while (running_ && GetPendingCount() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

private:
    void ProcessLoop() {
        while (running_ || !queue_.empty()) {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !queue_.empty() || !running_;
            });
            
            while (!queue_.empty()) {
                LogEntry entry = std::move(queue_.front());
                queue_.pop();
                lock.unlock();
                
                if (callback_) {
                    callback_(entry);
                }
                
                lock.lock();
            }
        }
    }
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<LogEntry> queue_;
    size_t max_queue_size_;
    std::atomic<bool> running_;
    std::atomic<LogLevel> min_level_{LogLevel::Info};
    LogCallback callback_;
    std::thread worker_;
};

//=============================================================================
// Convenience Macros
//=============================================================================

#define LOG(level, category, msg) Logger::Instance().Log(level, category, msg)
#define LOG_TRACE(cat, msg) LOG(LogLevel::Trace, cat, msg)
#define LOG_DEBUG(cat, msg) LOG(LogLevel::Debug, cat, msg)
#define LOG_INFO(cat, msg) LOG(LogLevel::Info, cat, msg)
#define LOG_WARN(cat, msg) LOG(LogLevel::Warn, cat, msg)
#define LOG_ERROR(cat, msg) LOG(LogLevel::Error, cat, msg)

} // namespace experiments

