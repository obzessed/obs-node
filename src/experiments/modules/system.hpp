#pragma once

/**
 * system.hpp - ModuleSystem and ModuleSystemFactory
 * 
 * Orchestrates module resolvers and loaders into a unified interface.
 */

#include <memory>
#include <optional>
#include <vector>
#include <string>

#include "module_info.hpp"
#include "resolvers.hpp"
#include "loaders.hpp"
#include "transformers.hpp"
#include "../core/logger.hpp"

namespace experiments {


//=============================================================================
// Module System - Orchestrates Resolvers and Loaders
//=============================================================================

class ModuleSystem {
public:
    ModuleSystem() = default;
    
    ModuleSystem(ModuleResolverPtr resolver, ModuleLoaderPtr loader)
        : resolver_(std::move(resolver)), loader_(std::move(loader)) {}
    
    // Set resolver (handles path resolution)
    void SetResolver(ModuleResolverPtr resolver) {
        resolver_ = std::move(resolver);
    }
    
    // Set loader (handles content loading)
    void SetLoader(ModuleLoaderPtr loader) {
        loader_ = std::move(loader);
    }
    
    // Resolve and load a module
    std::optional<ModuleInfo> Require(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) {
        if (!resolver_ || !loader_) {
            LOG_ERROR("ModuleSystem", "Resolver or loader not configured");
            return std::nullopt;
        }
        
        // Step 1: Resolve
        auto resolved = resolver_->Resolve(specifier, parent_path);
        if (!resolved) {
            LOG_DEBUG("ModuleSystem", "Could not resolve: " + specifier);
            return std::nullopt;
        }
        
        // Step 2: Load
        auto module_info = loader_->Load(resolved->resolved_path);
        if (!module_info) {
            LOG_DEBUG("ModuleSystem", "Could not load: " + resolved->resolved_path);
            return std::nullopt;
        }
        
        module_info->specifier = specifier;
        return module_info;
    }
    
    // Just resolve (without loading)
    std::optional<ResolveResult> Resolve(
        const std::string& specifier, 
        const std::string& parent_path = ""
    ) {
        if (!resolver_) return std::nullopt;
        return resolver_->Resolve(specifier, parent_path);
    }
    
    // Just load (from already-resolved path)
    std::optional<ModuleInfo> Load(const std::string& resolved_path) {
        if (!loader_) return std::nullopt;
        return loader_->Load(resolved_path);
    }
    
    ModuleResolverPtr GetResolver() const { return resolver_; }
    ModuleLoaderPtr GetLoader() const { return loader_; }
    
private:
    ModuleResolverPtr resolver_;
    ModuleLoaderPtr loader_;
};

//=============================================================================
// Module System Factory - Creates common configurations
//=============================================================================

class ModuleSystemFactory {
public:
    // Create standard module system (virtual -> disk)
    static std::shared_ptr<ModuleSystem> CreateStandard() {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create full module system (virtual -> npm -> jsr -> disk -> remote)
    static std::shared_ptr<ModuleSystem> CreateFull(
        const std::vector<std::string>& allowed_remote_prefixes = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<NpmResolver>());
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        auto remote_resolver = std::make_shared<RemoteResolver>();
        if (!allowed_remote_prefixes.empty()) {
            remote_resolver->SetAllowedPrefixes(allowed_remote_prefixes);
        }
        resolver_chain->AddResolver(remote_resolver);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create sandboxed module system (virtual only)
    static std::shared_ptr<ModuleSystem> CreateSandboxed() {
        return std::make_shared<ModuleSystem>(
            std::make_shared<VirtualResolver>(),
            std::make_shared<VirtualLoader>()
        );
    }
    
    // Create disk-only module system
    static std::shared_ptr<ModuleSystem> CreateDiskOnly(
        const std::vector<std::string>& search_paths = {"."}
    ) {
        return std::make_shared<ModuleSystem>(
            std::make_shared<DiskResolver>(search_paths),
            std::make_shared<DiskLoader>()
        );
    }
    
    // Create module system with npm: support
    static std::shared_ptr<ModuleSystem> CreateWithNpm(
        const std::string& node_modules_path = ""
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create module system with npm: and jsr: support
    static std::shared_ptr<ModuleSystem> CreateWithJsr(
        const std::string& node_modules_path = ""
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(std::make_shared<DiskLoader>());
        loader_chain->AddLoader(std::make_shared<RemoteLoader>());
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create module system with TypeScript transformation
    static std::shared_ptr<ModuleSystem> CreateWithTypeScript(
        const std::vector<std::string>& search_paths = {"."},
        TypeScriptTransformer::Options ts_options = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>(search_paths));
        
        // Create a transforming loader wrapping the disk loader
        auto disk_loader = std::make_shared<DiskLoader>();
        auto ts_transformer = std::make_shared<TypeScriptTransformer>(std::move(ts_options));
        auto transforming_loader = std::make_shared<TransformingLoader>(disk_loader, ts_transformer);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(transforming_loader);
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
    
    // Create full-featured module system with npm/jsr and TypeScript
    static std::shared_ptr<ModuleSystem> CreateTypedFull(
        const std::string& node_modules_path = "",
        TypeScriptTransformer::Options ts_options = {}
    ) {
        auto resolver_chain = std::make_shared<ResolverChain>();
        resolver_chain->AddResolver(std::make_shared<VirtualResolver>());
        auto npm_resolver = std::make_shared<NpmResolver>();
        if (!node_modules_path.empty()) {
            npm_resolver->SetNodeModulesPath(node_modules_path);
        }
        resolver_chain->AddResolver(npm_resolver);
        resolver_chain->AddResolver(std::make_shared<JsrResolver>());
        resolver_chain->AddResolver(std::make_shared<DiskResolver>());
        resolver_chain->AddResolver(std::make_shared<RemoteResolver>());
        
        // Create a transforming loader chain
        auto ts_transformer = std::make_shared<TypeScriptTransformer>(std::move(ts_options));
        
        auto disk_loader = std::make_shared<DiskLoader>();
        auto transforming_disk = std::make_shared<TransformingLoader>(disk_loader, ts_transformer);
        
        auto remote_loader = std::make_shared<RemoteLoader>();
        auto transforming_remote = std::make_shared<TransformingLoader>(remote_loader, ts_transformer);
        
        auto loader_chain = std::make_shared<LoaderChain>();
        loader_chain->AddLoader(std::make_shared<VirtualLoader>());
        loader_chain->AddLoader(transforming_disk);
        loader_chain->AddLoader(transforming_remote);
        
        return std::make_shared<ModuleSystem>(resolver_chain, loader_chain);
    }
};

// Backwards-compatible aliases
using ModuleLoaderFactory = ModuleSystemFactory;

} // namespace experiments
