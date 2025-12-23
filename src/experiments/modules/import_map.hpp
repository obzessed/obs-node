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
    std::unordered_map<std::string, std::string> imports;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> scopes;
    
    std::optional<std::string> Resolve(const std::string& specifier, 
                                        const std::string& parent_url = "") const {
        if (!parent_url.empty()) {
            for (const auto& [scope_prefix, scope_imports] : scopes) {
                if (parent_url.starts_with(scope_prefix)) {
                    if (auto it = scope_imports.find(specifier); it != scope_imports.end()) {
                        return it->second;
                    }
                    for (const auto& [key, value] : scope_imports) {
                        if (key.ends_with("/") && specifier.starts_with(key)) {
                            return value + specifier.substr(key.length());
                        }
                    }
                }
            }
        }
        
        if (auto it = imports.find(specifier); it != imports.end()) {
            return it->second;
        }
        
        for (const auto& [key, value] : imports) {
            if (key.ends_with("/") && specifier.starts_with(key)) {
                return value + specifier.substr(key.length());
            }
        }
        
        return std::nullopt;
    }
    
    static std::optional<ImportMap> FromJson(const std::string& json) {
        ImportMap map;
        // Simplified parser - just return empty map for now
        (void)json;
        return map;
    }
    
    bool IsEmpty() const {
        return imports.empty() && scopes.empty();
    }
    
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

//=============================================================================
// Import Map Resolver - Resolves using import maps
//=============================================================================

class ImportMapResolver : public ModuleResolver {
public:
    ImportMapResolver() = default;
    explicit ImportMapResolver(ImportMap import_map) 
        : import_map_(std::move(import_map)) {}
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path
    ) override {
        auto mapped = import_map_.Resolve(specifier, parent_path);
        if (mapped) {
            LOG_DEBUG("ImportMapResolver", "Mapped: " + specifier + " -> " + *mapped);
            return ResolveResult{*mapped, GetName(), false};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return import_map_.Resolve(specifier).has_value();
    }
    
    std::string GetName() const override { return "ImportMapResolver"; }
    
    void SetImportMap(ImportMap map) {
        import_map_ = std::move(map);
    }
    
    ImportMap& GetImportMap() { return import_map_; }
    const ImportMap& GetImportMap() const { return import_map_; }
    
    void AddMapping(const std::string& specifier, const std::string& resolved) {
        import_map_.imports[specifier] = resolved;
    }
    
    void AddScopedMapping(const std::string& scope, 
                          const std::string& specifier, 
                          const std::string& resolved) {
        import_map_.scopes[scope][specifier] = resolved;
    }
    
    void RemoveMapping(const std::string& specifier) {
        import_map_.imports.erase(specifier);
    }
    
    bool LoadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("ImportMapResolver", "Failed to open import map: " + path);
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        
        auto parsed = ImportMap::FromJson(buffer.str());
        if (parsed) {
            import_map_ = std::move(*parsed);
            LOG_INFO("ImportMapResolver", "Loaded import map from: " + path);
            return true;
        }
        
        LOG_ERROR("ImportMapResolver", "Failed to parse import map: " + path);
        return false;
    }
    
    bool LoadFromJson(const std::string& json) {
        auto parsed = ImportMap::FromJson(json);
        if (parsed) {
            import_map_ = std::move(*parsed);
            return true;
        }
        return false;
    }
    
private:
    ImportMap import_map_;
};

} // namespace experiments
