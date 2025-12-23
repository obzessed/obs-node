#pragma once

/**
 * module_info.hpp - Module Information Structures
 */

#include <string>
#include <optional>
#include <vector>
#include <sstream>

namespace experiments {

enum class ModuleFormat {
    CommonJS,   // require()
    ESModule,   // import/export
    JSON,       // JSON data
    Unknown     // Auto-detect based on extension or content
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

} // namespace experiments
