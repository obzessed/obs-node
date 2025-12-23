#pragma once

/**
 * environment_config.hpp - Environment Configuration
 */

#include <string>
#include <vector>
#include <chrono>
#include <memory>

#include "permissions.hpp"

namespace experiments {

// Forward declaration
class ModuleLoader;
using ModuleLoaderPtr = std::shared_ptr<ModuleLoader>;

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

} // namespace experiments
