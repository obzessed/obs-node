#pragma once

/**
 * import_map.hpp - Import Map Support
 */

#include <string>
#include <optional>
#include <unordered_map>
#include <fstream>
#include <sstream>

#include "resolvers.hpp"
#include "../core/logger.hpp"

namespace experiments {

//=============================================================================
// Import Map - Maps bare specifiers to URLs/paths
//=============================================================================

struct ImportMap {
    // Direct mappings: "lodash" -> "./vendor/lodash/index.js"
    std::unordered_map<std::string, std::string> imports;

    // Scoped mappings: "/app/" -> {"lodash" -> "./vendor/lodash-custom.js"}
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> scopes;

    // Resolve a specifier with optional scope context
    std::optional<std::string> Resolve(const std::string& specifier,
                                        const std::string& parent_url = "") const {
        // Check scoped mappings first
        if (!parent_url.empty()) {
            for (const auto& [scope_prefix, scope_imports] : scopes) {
                if (parent_url.starts_with(scope_prefix)) {
                    // Check exact match
                    if (auto it = scope_imports.find(specifier); it != scope_imports.end()) {
                        return it->second;
                    }
                    // Check prefix mapping (e.g., "lodash/" -> "./vendor/lodash/")
                    for (const auto& [key, value] : scope_imports) {
                        if (key.ends_with("/") && specifier.starts_with(key)) {
                            return value + specifier.substr(key.length());
                        }
                    }
                }
            }
        }

        // Check global imports
        if (auto it = imports.find(specifier); it != imports.end()) {
            return it->second;
        }

        // Check prefix mappings in global imports
        for (const auto& [key, value] : imports) {
            if (key.ends_with("/") && specifier.starts_with(key)) {
                return value + specifier.substr(key.length());
            }
        }

        return std::nullopt;
    }

    // Parse import map from JSON string (simplified parser)
    static std::optional<ImportMap> FromJson(const std::string& json) {
        ImportMap map;

        // Simple JSON parsing (in production, use a proper JSON library)
        // Expected format:
        // {
        //   "imports": { "lodash": "./vendor/lodash.js" },
        //   "scopes": { "/app/": { "lodash": "./custom/lodash.js" } }
        // }

        auto findValue = [&json](const std::string& key, size_t start = 0)
            -> std::pair<size_t, size_t> {
            std::string search = "\"" + key + "\"";
            size_t pos = json.find(search, start);
            if (pos == std::string::npos) return {std::string::npos, 0};

            pos = json.find(':', pos);
            if (pos == std::string::npos) return {std::string::npos, 0};

            pos = json.find_first_not_of(" \t\n\r", pos + 1);
            return {pos, 0};
        };

        auto extractString = [&json](size_t& pos) -> std::string {
            if (pos >= json.size() || json[pos] != '"') return "";
            size_t end = json.find('"', pos + 1);
            if (end == std::string::npos) return "";
            std::string result = json.substr(pos + 1, end - pos - 1);
            pos = end + 1;
            return result;
        };

        auto parseObject = [&](size_t start)
            -> std::pair<std::unordered_map<std::string, std::string>, size_t> {
            std::unordered_map<std::string, std::string> result;
            size_t pos = json.find('{', start);
            if (pos == std::string::npos) return {result, start};
            pos++;

            while (pos < json.size()) {
                pos = json.find_first_not_of(" \t\n\r,", pos);
                if (pos == std::string::npos || json[pos] == '}') break;

                std::string key = extractString(pos);
                if (key.empty()) break;

                pos = json.find(':', pos);
                if (pos == std::string::npos) break;
                pos = json.find_first_not_of(" \t\n\r", pos + 1);

                std::string value = extractString(pos);
                if (!key.empty() && !value.empty()) {
                    result[key] = value;
                }
            }

            return {result, json.find('}', pos) + 1};
        };

        // Parse "imports"
        auto [imports_pos, _1] = findValue("imports");
        if (imports_pos != std::string::npos) {
            auto [imports_map, end] = parseObject(imports_pos);
            map.imports = std::move(imports_map);
        }

        // Parse "scopes" (nested objects)
        auto [scopes_pos, _2] = findValue("scopes");
        if (scopes_pos != std::string::npos) {
            size_t pos = json.find('{', scopes_pos);
            if (pos != std::string::npos) {
                pos++;
                while (pos < json.size()) {
                    pos = json.find_first_not_of(" \t\n\r,", pos);
                    if (pos == std::string::npos || json[pos] == '}') break;

                    std::string scope_key = extractString(pos);
                    if (scope_key.empty()) break;

                    pos = json.find(':', pos);
                    if (pos == std::string::npos) break;
                    pos = json.find_first_not_of(" \t\n\r", pos + 1);

                    auto [scope_imports, end] = parseObject(pos);
                    if (!scope_key.empty()) {
                        map.scopes[scope_key] = std::move(scope_imports);
                    }
                    pos = end;
                }
            }
        }

        return map;
    }

    // Convert to JSON string
    std::string ToJson() const {
        std::ostringstream oss;
        oss << "{\n  \"imports\": {";

        bool first = true;
        for (const auto& [key, value] : imports) {
            if (!first) oss << ",";
            oss << "\n    \"" << key << "\": \"" << value << "\"";
            first = false;
        }
        oss << "\n  }";

        if (!scopes.empty()) {
            oss << ",\n  \"scopes\": {";
            bool first_scope = true;
            for (const auto& [scope, scope_imports] : scopes) {
                if (!first_scope) oss << ",";
                oss << "\n    \"" << scope << "\": {";

                bool first_import = true;
                for (const auto& [key, value] : scope_imports) {
                    if (!first_import) oss << ",";
                    oss << "\n      \"" << key << "\": \"" << value << "\"";
                    first_import = false;
                }
                oss << "\n    }";
                first_scope = false;
            }
            oss << "\n  }";
        }

        oss << "\n}";
        return oss.str();
    }

    // Check if empty
    bool IsEmpty() const {
        return imports.empty() && scopes.empty();
    }

    // Merge another import map (other takes precedence)
    void Merge(const ImportMap& other) {
        for (const auto& [key, value] : other.imports) {
            imports[key] = value;
        }
        for (const auto& [scope, scope_imports] : other.scopes) {
            for (const auto& [key, value] : scope_imports) {
                scopes[scope][key] = value;
            }
        }
    }
};

} // namespace experiments
