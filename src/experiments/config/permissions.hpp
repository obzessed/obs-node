#pragma once

/**
 * permissions.hpp - Script Permissions System
 */

#include <string>
#include <vector>
#include <cstdint>

namespace experiments {

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

} // namespace experiments
