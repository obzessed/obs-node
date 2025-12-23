#pragma once

/**
 * resolvers.hpp - Module Resolution Classes
 */

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_set>
#include <shared_mutex>
#include <filesystem>
#include <algorithm>

#include "module_info.hpp"
#include "../core/logger.hpp"

namespace experiments {

//=============================================================================
// Module Resolver - Abstract base class for path resolution
//=============================================================================

class ModuleResolver {
public:
    virtual ~ModuleResolver() = default;
    
    virtual std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) = 0;
    
    virtual bool CanHandle(const std::string& specifier) const = 0;
    virtual std::string GetName() const = 0;
};

using ModuleResolverPtr = std::shared_ptr<ModuleResolver>;

//=============================================================================
// Disk Resolver - Resolves modules from file system
//=============================================================================

class DiskResolver : public ModuleResolver {
public:
    explicit DiskResolver(std::vector<std::string> search_paths = {"."}) 
        : search_paths_(std::move(search_paths)) {}
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path
    ) override {
        namespace fs = std::filesystem;
        
        std::optional<std::string> resolved;
        
        if (fs::path(specifier).is_absolute()) {
            resolved = TryResolve(specifier);
        }
        else if (specifier.starts_with("./") || specifier.starts_with("../")) {
            fs::path parent_dir = parent_path.empty() 
                ? fs::current_path() 
                : fs::path(parent_path).parent_path();
            resolved = TryResolve((parent_dir / specifier).string());
        }
        else {
            for (const auto& search_path : search_paths_) {
                resolved = TryResolve((fs::path(search_path) / specifier).string());
                if (resolved) break;
            }
        }
        
        if (resolved) {
            return ResolveResult{*resolved, GetName(), false};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return !specifier.starts_with("http://") && 
               !specifier.starts_with("https://") &&
               !specifier.starts_with("virtual:");
    }
    
    std::string GetName() const override { return "DiskResolver"; }
    
    void AddSearchPath(const std::string& path) {
        search_paths_.push_back(path);
    }
    
    const std::vector<std::string>& GetSearchPaths() const {
        return search_paths_;
    }
    
private:
    std::vector<std::string> search_paths_;
    
    std::optional<std::string> TryResolve(const std::string& path) {
        namespace fs = std::filesystem;
        
        if (fs::exists(path) && fs::is_regular_file(path)) {
            return fs::absolute(path).string();
        }
        
        static const std::vector<std::string> extensions = {
            ".js", ".mjs", ".cjs", ".json", ".node", ".ts", ".mts", ".cts"
        };
        for (const auto& ext : extensions) {
            std::string with_ext = path + ext;
            if (fs::exists(with_ext) && fs::is_regular_file(with_ext)) {
                return fs::absolute(with_ext).string();
            }
        }
        
        if (fs::exists(path) && fs::is_directory(path)) {
            for (const auto& ext : extensions) {
                std::string index_path = (fs::path(path) / ("index" + ext)).string();
                if (fs::exists(index_path)) {
                    return fs::absolute(index_path).string();
                }
            }
            std::string pkg_path = (fs::path(path) / "package.json").string();
            if (fs::exists(pkg_path)) {
                return TryResolve((fs::path(path) / "index.js").string());
            }
        }
        
        return std::nullopt;
    }
};

//=============================================================================
// Virtual Resolver - Resolves modules from in-memory registry
//=============================================================================

class VirtualResolver : public ModuleResolver {
public:
    explicit VirtualResolver(std::string prefix = "virtual:")
        : prefix_(std::move(prefix)) {}
    
    void Register(const std::string& name) {
        std::lock_guard lock(mutex_);
        registered_.insert(name);
    }
    
    void Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        registered_.erase(name);
    }
    
    bool Has(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return registered_.contains(name);
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        std::shared_lock lock(mutex_);
        
        std::string name = specifier;
        if (name.starts_with(prefix_)) {
            name = name.substr(prefix_.length());
        }
        
        if (registered_.contains(name)) {
            return ResolveResult{prefix_ + name, GetName(), true};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        if (specifier.starts_with(prefix_)) return true;
        std::shared_lock lock(mutex_);
        return registered_.contains(specifier);
    }
    
    std::string GetName() const override { return "VirtualResolver"; }
    std::string GetPrefix() const { return prefix_; }
    void SetPrefix(std::string prefix) { prefix_ = std::move(prefix); }
    
private:
    std::string prefix_;
    mutable std::shared_mutex mutex_;
    std::unordered_set<std::string> registered_;
};

//=============================================================================
// Remote Resolver - Resolves HTTP/HTTPS URLs
//=============================================================================

class RemoteResolver : public ModuleResolver {
public:
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        if (specifier.starts_with("http://") || specifier.starts_with("https://")) {
            if (!allowed_prefixes_.empty()) {
                bool allowed = false;
                for (const auto& prefix : allowed_prefixes_) {
                    if (specifier.starts_with(prefix)) {
                        allowed = true;
                        break;
                    }
                }
                if (!allowed) {
                    LOG_WARN("RemoteResolver", "URL not in allowed prefixes: " + specifier);
                    return std::nullopt;
                }
            }
            return ResolveResult{specifier, GetName(), false};
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("http://") || specifier.starts_with("https://");
    }
    
    std::string GetName() const override { return "RemoteResolver"; }
    
    void SetAllowedPrefixes(std::vector<std::string> prefixes) {
        allowed_prefixes_ = std::move(prefixes);
    }
    
private:
    std::vector<std::string> allowed_prefixes_;
};

//=============================================================================
// NPM Package Spec
//=============================================================================

struct NpmPackageSpec {
    std::string name;
    std::string version;
    std::string subpath;
    
    static std::optional<NpmPackageSpec> Parse(const std::string& specifier) {
        if (!specifier.starts_with("npm:")) return std::nullopt;
        
        std::string rest = specifier.substr(4);
        NpmPackageSpec spec;
        
        size_t name_end = 0;
        if (rest.starts_with("@")) {
            size_t slash_pos = rest.find('/');
            if (slash_pos == std::string::npos) return std::nullopt;
            name_end = rest.find_first_of("/@", slash_pos + 1);
        } else {
            name_end = rest.find_first_of("/@");
        }
        
        if (name_end == std::string::npos) {
            spec.name = rest;
            spec.version = "latest";
            return spec;
        }
        
        spec.name = rest.substr(0, name_end);
        rest = rest.substr(name_end);
        
        if (rest.starts_with("@")) {
            size_t version_end = rest.find('/');
            if (version_end == std::string::npos) {
                spec.version = rest.substr(1);
            } else {
                spec.version = rest.substr(1, version_end - 1);
                spec.subpath = rest.substr(version_end);
            }
        } else if (rest.starts_with("/")) {
            spec.version = "latest";
            spec.subpath = rest;
        }
        
        if (spec.version.empty()) spec.version = "latest";
        return spec;
    }
};

//=============================================================================
// NPM Resolver
//=============================================================================

class NpmResolver : public ModuleResolver {
public:
    void SetRegistry(std::string registry) {
        registry_ = std::move(registry);
        if (!registry_.ends_with("/")) registry_ += "/";
    }
    
    void SetNodeModulesPath(std::string path) {
        node_modules_path_ = std::move(path);
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        auto pkg = NpmPackageSpec::Parse(specifier);
        if (!pkg) return std::nullopt;
        
        if (!node_modules_path_.empty()) {
            namespace fs = std::filesystem;
            fs::path local_path = fs::path(node_modules_path_) / pkg->name;
            if (fs::exists(local_path)) {
                std::string resolved = local_path.string();
                if (!pkg->subpath.empty()) {
                    resolved += pkg->subpath;
                }
                return ResolveResult{resolved, GetName(), false};
            }
        }
        
        std::string cdn_url = cdn_base_ + pkg->name;
        if (pkg->version != "latest") {
            cdn_url += "@" + pkg->version;
        }
        if (!pkg->subpath.empty()) {
            cdn_url += pkg->subpath;
        }
        
        return ResolveResult{cdn_url, GetName(), false};
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("npm:");
    }
    
    std::string GetName() const override { return "NpmResolver"; }
    
    void SetCdnBase(std::string cdn) {
        cdn_base_ = std::move(cdn);
        if (!cdn_base_.ends_with("/")) cdn_base_ += "/";
    }
    
private:
    std::string registry_{"https://registry.npmjs.org/"};
    std::string node_modules_path_;
    std::string cdn_base_{"https://esm.sh/"};
};

//=============================================================================
// JSR Package Spec
//=============================================================================

struct JsrPackageSpec {
    std::string scope;
    std::string name;
    std::string version;
    std::string subpath;
    
    static std::optional<JsrPackageSpec> Parse(const std::string& specifier) {
        if (!specifier.starts_with("jsr:")) return std::nullopt;
        
        std::string rest = specifier.substr(4);
        JsrPackageSpec spec;
        
        if (!rest.starts_with("@")) return std::nullopt;
        
        size_t slash_pos = rest.find('/');
        if (slash_pos == std::string::npos) return std::nullopt;
        spec.scope = rest.substr(0, slash_pos);
        rest = rest.substr(slash_pos + 1);
        
        size_t at_pos = rest.find('@');
        size_t path_pos = rest.find('/');
        
        if (at_pos != std::string::npos && (path_pos == std::string::npos || at_pos < path_pos)) {
            spec.name = rest.substr(0, at_pos);
            rest = rest.substr(at_pos + 1);
            
            path_pos = rest.find('/');
            if (path_pos != std::string::npos) {
                spec.version = rest.substr(0, path_pos);
                spec.subpath = rest.substr(path_pos);
            } else {
                spec.version = rest;
            }
        } else if (path_pos != std::string::npos) {
            spec.name = rest.substr(0, path_pos);
            spec.subpath = rest.substr(path_pos);
        } else {
            spec.name = rest;
        }
        
        return spec;
    }
};

//=============================================================================
// JSR Resolver
//=============================================================================

class JsrResolver : public ModuleResolver {
public:
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& /*parent_path*/
    ) override {
        auto pkg = JsrPackageSpec::Parse(specifier);
        if (!pkg) return std::nullopt;
        
        std::string url = jsr_base_ + pkg->scope + "/" + pkg->name;
        if (!pkg->version.empty()) {
            url += "@" + pkg->version;
        }
        if (!pkg->subpath.empty()) {
            url += pkg->subpath;
        } else {
            url += "/mod.ts";
        }
        
        return ResolveResult{url, GetName(), false};
    }
    
    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("jsr:");
    }
    
    std::string GetName() const override { return "JsrResolver"; }
    
    void SetJsrBase(std::string base) {
        jsr_base_ = std::move(base);
        if (!jsr_base_.ends_with("/")) jsr_base_ += "/";
    }
    
private:
    std::string jsr_base_{"https://jsr.io/"};
};

//=============================================================================
// Resolver Chain - Combines multiple resolvers in priority order
//=============================================================================

class ResolverChain : public ModuleResolver {
public:
    ResolverChain() = default;
    
    explicit ResolverChain(std::vector<ModuleResolverPtr> resolvers) 
        : resolvers_(std::move(resolvers)) {}
    
    void AddResolver(ModuleResolverPtr resolver) {
        resolvers_.push_back(std::move(resolver));
    }
    
    void AddResolverFirst(ModuleResolverPtr resolver) {
        resolvers_.insert(resolvers_.begin(), std::move(resolver));
    }
    
    void RemoveResolver(const std::string& name) {
        resolvers_.erase(
            std::remove_if(resolvers_.begin(), resolvers_.end(), 
                [&name](const auto& r) { return r->GetName() == name; }),
            resolvers_.end()
        );
    }
    
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path
    ) override {
        for (const auto& resolver : resolvers_) {
            if (resolver->CanHandle(specifier)) {
                auto result = resolver->Resolve(specifier, parent_path);
                if (result) {
                    LOG_DEBUG("ResolverChain", 
                        result->resolver_name + " resolved: " + specifier + " -> " + result->resolved_path);
                    return result;
                }
            }
        }
        return std::nullopt;
    }
    
    bool CanHandle(const std::string& specifier) const override {
        for (const auto& resolver : resolvers_) {
            if (resolver->CanHandle(specifier)) return true;
        }
        return false;
    }
    
    std::string GetName() const override { return "ResolverChain"; }
    
    std::vector<std::string> GetResolverNames() const {
        std::vector<std::string> names;
        for (const auto& resolver : resolvers_) {
            names.push_back(resolver->GetName());
        }
        return names;
    }
    
private:
    std::vector<ModuleResolverPtr> resolvers_;
};

} // namespace experiments
