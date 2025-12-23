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

// Source Map - Represents a JavaScript/TypeScript source map
// Must be defined before ModuleInfo since std::optional requires complete type
struct SourceMap {
    int version{3};
    std::string file;
    std::string source_root;
    std::vector<std::string> sources;
    std::vector<std::string> sources_content;
    std::vector<std::string> names;
    std::string mappings;
    
    bool is_valid() const { return version == 3 && !mappings.empty(); }
    
    std::string ToInlineUrl() const {
        std::string json = ToJson();
        return "//# sourceMappingURL=data:application/json;base64," + json;
    }
    
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
    
    static std::optional<SourceMap> FromJson(const std::string& /*json*/) {
        SourceMap map;
        map.version = 3;
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
    std::optional<SourceMap> source_map; // Optional source map
    
    bool is_valid() const { return !source.empty(); }
    bool was_transformed() const { return transformed && !original_source.empty(); }
};

struct ResolveResult {
    std::string resolved_path;
    std::string resolver_name;  // Which resolver found it
    bool is_virtual{false};     // Is this a virtual module?
    
    bool is_valid() const { return !resolved_path.empty(); }
    operator bool() const { return is_valid(); }
};

// Transform Result - Output from a module transformer

struct TransformResult {
    std::string code;
    std::optional<SourceMap> source_map;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    bool is_ok() const { return errors.empty(); }
    
    std::string GetCodeWithInlineSourceMap() const {
        if (source_map && source_map->is_valid()) {
            return code + "\n" + source_map->ToInlineUrl();
        }
        return code;
    }
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
