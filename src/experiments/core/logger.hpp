#pragma once

/**
 * logger.hpp - Logging System
 */

#include <iostream>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>

namespace experiments {

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

// Logging macros
#define LOG(level, category, msg) ::experiments::Logger::Instance().Log(level, category, msg)
#define LOG_TRACE(cat, msg) LOG(::experiments::LogLevel::Trace, cat, msg)
#define LOG_DEBUG(cat, msg) LOG(::experiments::LogLevel::Debug, cat, msg)
#define LOG_INFO(cat, msg) LOG(::experiments::LogLevel::Info, cat, msg)
#define LOG_WARN(cat, msg) LOG(::experiments::LogLevel::Warn, cat, msg)
#define LOG_ERROR(cat, msg) LOG(::experiments::LogLevel::Error, cat, msg)

} // namespace experiments
