#pragma once

/**
 * sandbox.hpp - Sandbox Configuration and Guard
 */

#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <algorithm>

#include "audit.hpp"

namespace experiments {

//=============================================================================
// Sandbox Configuration - Prevent sandbox escapes
//=============================================================================

struct SandboxConfig {
    // What to freeze/disable
    bool freeze_intrinsics{true};          // Freeze Object, Array, etc. prototypes
    bool freeze_global{false};              // Freeze globalThis
    bool disable_eval{true};                // Block eval()
    bool disable_function_constructor{true}; // Block new Function()
    bool disable_wasm{false};               // Block WebAssembly

    // What to hide
    bool hide_process{true};                // Hide process object
    bool hide_require{false};               // Hide require (use custom)
    bool hide_module{true};                 // Hide module object
    bool hide_global{false};                // Hide global object

    // What to wrap
    bool wrap_timers{true};                 // Wrap setTimeout/setInterval
    bool wrap_console{true};                // Wrap console for capture
    bool wrap_fetch{true};                  // Wrap fetch for monitoring

    // What to allow
    std::unordered_set<std::string> allowed_globals;   // Whitelist of globals
    std::unordered_set<std::string> blocked_globals;   // Blacklist of globals
    std::unordered_set<std::string> allowed_requires;  // Whitelist of requires
    std::unordered_set<std::string> blocked_requires;  // Blacklist of requires

    // Path restrictions
    std::vector<std::string> allowed_read_paths;   // File read whitelist
    std::vector<std::string> allowed_write_paths;  // File write whitelist
    std::vector<std::string> blocked_paths;        // Blocked paths

    // Network restrictions
    std::vector<std::string> allowed_hosts;        // Network whitelist
    std::vector<std::string> blocked_hosts;        // Network blacklist
    std::vector<uint16_t> allowed_ports;           // Allowed ports
    std::vector<uint16_t> blocked_ports;           // Blocked ports

    // Presets
    static SandboxConfig Strict() {
        SandboxConfig config;
        config.freeze_intrinsics = true;
        config.freeze_global = true;
        config.disable_eval = true;
        config.disable_function_constructor = true;
        config.disable_wasm = true;
        config.hide_process = true;
        config.hide_require = true;
        config.hide_module = true;
        return config;
    }

    static SandboxConfig Standard() {
        return SandboxConfig{};
    }

    static SandboxConfig Permissive() {
        SandboxConfig config;
        config.freeze_intrinsics = false;
        config.disable_eval = false;
        config.disable_function_constructor = false;
        config.hide_process = false;
        config.hide_require = false;
        return config;
    }
};

// Runtime sandbox guard
class SandboxGuard {
public:
    SandboxGuard(SandboxConfig config, std::shared_ptr<AuditLogger> logger = nullptr)
        : config_(std::move(config))
        , logger_(std::move(logger)) {}

    // Check if a global access is allowed
    bool CheckGlobalAccess(const std::string& name) const {
        if (!config_.allowed_globals.empty()) {
            if (!config_.allowed_globals.contains(name)) {
                LogViolation("global access", name);
                return false;
            }
        }
        if (config_.blocked_globals.contains(name)) {
            LogViolation("global blocked", name);
            return false;
        }
        return true;
    }

    // Check if a require is allowed
    bool CheckRequire(const std::string& specifier) const {
        if (!config_.allowed_requires.empty()) {
            if (!config_.allowed_requires.contains(specifier)) {
                LogViolation("require blocked", specifier);
                return false;
            }
        }
        if (config_.blocked_requires.contains(specifier)) {
            LogViolation("require blocked", specifier);
            return false;
        }
        return true;
    }

    // Check file access
    bool CheckFileRead(const std::string& path) const {
        return CheckPathAccess(path, config_.allowed_read_paths, "file read");
    }

    bool CheckFileWrite(const std::string& path) const {
        return CheckPathAccess(path, config_.allowed_write_paths, "file write");
    }

    // Check network access
    bool CheckNetworkAccess(const std::string& host, uint16_t port) const {
        // Check blocked hosts
        for (const auto& blocked : config_.blocked_hosts) {
            if (host.find(blocked) != std::string::npos) {
                LogViolation("network blocked host", host);
                return false;
            }
        }

        // Check allowed hosts
        if (!config_.allowed_hosts.empty()) {
            bool allowed = false;
            for (const auto& allowed_host : config_.allowed_hosts) {
                if (host.find(allowed_host) != std::string::npos) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                LogViolation("network host not allowed", host);
                return false;
            }
        }

        // Check blocked ports
        if (std::find(config_.blocked_ports.begin(), config_.blocked_ports.end(), port)
            != config_.blocked_ports.end()) {
            LogViolation("network blocked port", std::to_string(port));
            return false;
        }

        return true;
    }

    // Check if eval is allowed
    bool CheckEval() const {
        if (config_.disable_eval) {
            LogViolation("eval", "disabled");
            return false;
        }
        return true;
    }

    const SandboxConfig& GetConfig() const { return config_; }

private:
    SandboxConfig config_;
    std::shared_ptr<AuditLogger> logger_;

    bool CheckPathAccess(const std::string& path,
                         const std::vector<std::string>& allowed,
                         const std::string& op) const {
        // Always block dangerous paths
        if (path.find("..") != std::string::npos) {
            LogViolation(op + " path traversal", path);
            return false;
        }

        // Check blocked paths
        for (const auto& blocked : config_.blocked_paths) {
            if (path.starts_with(blocked)) {
                LogViolation(op + " blocked path", path);
                return false;
            }
        }

        // Check allowed paths
        if (!allowed.empty()) {
            bool in_allowed = false;
            for (const auto& allowed_path : allowed) {
                if (path.starts_with(allowed_path)) {
                    in_allowed = true;
                    break;
                }
            }
            if (!in_allowed) {
                LogViolation(op + " not in allowed paths", path);
                return false;
            }
        }
        
        return true;
    }
    
    void LogViolation(const std::string& type, const std::string& target) const {
        if (logger_) {
            logger_->LogSandboxViolation(type + ": " + target);
        }
    }
};

} // namespace experiments
