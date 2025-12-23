#pragma once

/**
 * loaders.hpp - Module Loading Classes
 */

#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>

#include "module_info.hpp"
#include "../core/logger.hpp"
#include "transformers.hpp"

namespace experiments {

//=============================================================================
// Module Loader - Abstract base class for loading module content
//=============================================================================

class ModuleLoader {
public:
    virtual ~ModuleLoader() = default;

    // Load module from resolved path (returns source code)
    virtual std::optional<ModuleInfo> Load(const std::string& resolved_path) = 0;

    // Check if this loader can load from the resolved path
    virtual bool CanLoad(const std::string& resolved_path) const = 0;

    // Get loader name
    virtual std::string GetName() const = 0;
};

using ModuleLoaderPtr = std::shared_ptr<ModuleLoader>;

//=============================================================================
// Disk Loader - Loads module content from file system
//=============================================================================

class DiskLoader : public ModuleLoader {
public:
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        std::ifstream file(resolved_path);
        if (!file.is_open()) return std::nullopt;

        std::stringstream buffer;
        buffer << file.rdbuf();

        ModuleInfo info;
        info.resolved_path = resolved_path;
        info.source = buffer.str();
        info.format = ModuleUtils::DetectFormatFromPath(resolved_path);
        if (info.format == ModuleFormat::Unknown) {
            info.format = ModuleUtils::DetectFormatFromContent(info.source);
        }

        return info;
    }

    bool CanLoad(const std::string& resolved_path) const override {
        return !resolved_path.starts_with("http://") &&
               !resolved_path.starts_with("https://") &&
               !resolved_path.starts_with("virtual:");
    }

    std::string GetName() const override { return "DiskLoader"; }
};

//=============================================================================
// Virtual Loader - Loads module content from in-memory storage
//=============================================================================

class VirtualLoader : public ModuleLoader {
public:
    explicit VirtualLoader(std::string prefix = "virtual:")
        : prefix_(std::move(prefix)) {}

    // Register module content
    void Register(const std::string& name, const std::string& source,
                  ModuleFormat format = ModuleFormat::CommonJS) {
        std::lock_guard lock(mutex_);
        modules_[name] = {name, prefix_ + name, source, "", format};
    }

    void RegisterAll(const std::unordered_map<std::string, std::string>& modules) {
        for (const auto& [name, source] : modules) {
            Register(name, source);
        }
    }

    void Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        modules_.erase(name);
    }

    void Clear() {
        std::lock_guard lock(mutex_);
        modules_.clear();
    }

    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        std::shared_lock lock(mutex_);

        std::string name = resolved_path;
        if (name.starts_with(prefix_)) {
            name = name.substr(prefix_.length());
        }

        auto it = modules_.find(name);
        if (it != modules_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool CanLoad(const std::string& resolved_path) const override {
        if (resolved_path.starts_with(prefix_)) return true;
        std::shared_lock lock(mutex_);
        return modules_.contains(resolved_path);
    }

    std::string GetName() const override { return "VirtualLoader"; }
    std::string GetPrefix() const { return prefix_; }

    std::vector<std::string> ListModules() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : modules_) {
            names.push_back(name);
        }
        return names;
    }

    bool Has(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return modules_.contains(name);
    }

    size_t Count() const {
        std::shared_lock lock(mutex_);
        return modules_.size();
    }

private:
    std::string prefix_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ModuleInfo> modules_;
};

//=============================================================================
// Remote Loader - Loads module content from HTTP/HTTPS
//=============================================================================

class RemoteLoader : public ModuleLoader {
public:
    struct CacheEntry {
        ModuleInfo info;
        std::chrono::steady_clock::time_point cached_at;
    };

    explicit RemoteLoader(std::chrono::seconds cache_ttl = std::chrono::seconds(3600))
        : cache_ttl_(cache_ttl) {}

    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        // Check cache first
        {
            std::shared_lock lock(cache_mutex_);
            auto it = cache_.find(resolved_path);
            if (it != cache_.end()) {
                auto age = std::chrono::steady_clock::now() - it->second.cached_at;
                if (age < cache_ttl_) {
                    return it->second.info;
                }
            }
        }

        // Fetch from remote
        auto source = FetchUrl(resolved_path);
        if (!source) return std::nullopt;

        ModuleInfo info;
        info.resolved_path = resolved_path;
        info.source = *source;
        info.format = ModuleUtils::DetectFormatFromPath(resolved_path);
        if (info.format == ModuleFormat::Unknown) {
            info.format = ModuleUtils::DetectFormatFromContent(info.source);
        }

        // Cache the result
        {
            std::lock_guard lock(cache_mutex_);
            cache_[resolved_path] = {info, std::chrono::steady_clock::now()};
        }

        return info;
    }

    bool CanLoad(const std::string& resolved_path) const override {
        return resolved_path.starts_with("http://") || resolved_path.starts_with("https://");
    }

    std::string GetName() const override { return "RemoteLoader"; }

    void ClearCache() {
        std::lock_guard lock(cache_mutex_);
        cache_.clear();
    }

private:
    std::chrono::seconds cache_ttl_;
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    std::optional<std::string> FetchUrl(const std::string& url) {
        // Placeholder - implement with libcurl in production
        LOG_INFO("RemoteLoader", "Would fetch: " + url);
        return std::nullopt;
    }
};

//=============================================================================
// Native Module Support - Load .node/.dll/.so files
//=============================================================================

// Platform-specific definitions
#ifdef _WIN32
    #include <windows.h>
    using NativeHandle = HMODULE;
    #define NATIVE_EXT ".dll"
#else
    #include <dlfcn.h>
    using NativeHandle = void*;
    #ifdef __APPLE__
        #define NATIVE_EXT ".dylib"
    #else
        #define NATIVE_EXT ".so"
    #endif
#endif

struct NativeModuleInfo {
    std::string name;
    std::string path;
    std::string version;
    NativeHandle handle{nullptr};
    bool loaded{false};
    std::chrono::steady_clock::time_point loaded_at;

    // N-API module info
    int napi_version{0};
    std::string description;
    std::vector<std::string> exports;

    NativeModuleInfo() = default;
    NativeModuleInfo(std::string n, std::string p)
        : name(std::move(n)), path(std::move(p)) {}

    bool IsValid() const { return loaded && handle != nullptr; }
};

// Native module initialization function types
using NapiInitFn = void* (*)(void* env, void* exports);
using NodeInitFn = void (*)(void* exports, void* module, void* priv);

class NativeModuleLoader {
public:
    struct Options {
        std::vector<std::string> search_paths;
        bool allow_absolute_paths{false};
        bool verify_signatures{false};
        std::vector<std::string> allowed_modules;  // Empty = all allowed
        std::vector<std::string> blocked_modules;
    };

    explicit NativeModuleLoader(Options options = {})
        : options_(std::move(options)) {}

    ~NativeModuleLoader() {
        UnloadAll();
    }

    // Load a native module
    std::optional<NativeModuleInfo> Load(const std::string& module_path) {
        std::lock_guard lock(mutex_);

        // Check if already loaded
        auto it = loaded_.find(module_path);
        if (it != loaded_.end() && it->second.IsValid()) {
            return it->second;
        }

        // Resolve the path
        std::string resolved = ResolvePath(module_path);
        if (resolved.empty()) {
            LOG_ERROR("NativeModuleLoader", "Module not found: " + module_path);
            return std::nullopt;
        }

        // Security check
        if (!IsAllowed(module_path)) {
            LOG_ERROR("NativeModuleLoader", "Module blocked: " + module_path);
            return std::nullopt;
        }

        // Load the library
        NativeHandle handle = LoadLibraryPlatform(resolved);
        if (!handle) {
            LOG_ERROR("NativeModuleLoader", "Failed to load: " + resolved + " - " + GetLastErrorString());
            return std::nullopt;
        }

        NativeModuleInfo info(module_path, resolved);
        info.handle = handle;
        info.loaded = true;
        info.loaded_at = std::chrono::steady_clock::now();

        // Try to find N-API init function
        auto napi_init = reinterpret_cast<NapiInitFn>(GetSymbol(handle, "napi_register_module_v1"));
        if (napi_init) {
            info.napi_version = 1;
            LOG_INFO("NativeModuleLoader", "Loaded N-API module: " + module_path);
        } else {
            // Try legacy node init
            auto node_init = reinterpret_cast<NodeInitFn>(GetSymbol(handle, "node_register_module_v1"));
            if (node_init) {
                LOG_INFO("NativeModuleLoader", "Loaded legacy Node module: " + module_path);
            }
        }

        loaded_[module_path] = info;
        return info;
    }

    // Unload a native module
    bool Unload(const std::string& module_path) {
        std::lock_guard lock(mutex_);

        auto it = loaded_.find(module_path);
        if (it == loaded_.end()) return false;

        if (it->second.handle) {
            UnloadLibraryPlatform(it->second.handle);
        }

        loaded_.erase(it);
        return true;
    }

    // Unload all modules
    void UnloadAll() {
        std::lock_guard lock(mutex_);

        for (auto& [_, info] : loaded_) {
            if (info.handle) {
                UnloadLibraryPlatform(info.handle);
            }
        }
        loaded_.clear();
    }

    // Check if a module is loaded
    bool IsLoaded(const std::string& module_path) const {
        std::shared_lock lock(mutex_);
        auto it = loaded_.find(module_path);
        return it != loaded_.end() && it->second.IsValid();
    }

    // Get loaded module info
    std::optional<NativeModuleInfo> GetInfo(const std::string& module_path) const {
        std::shared_lock lock(mutex_);
        auto it = loaded_.find(module_path);
        if (it != loaded_.end()) return it->second;
        return std::nullopt;
    }

    // Get all loaded modules
    std::vector<std::string> GetLoadedModules() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [path, _] : loaded_) {
            result.push_back(path);
        }
        return result;
    }

    // Get a symbol from a loaded module
    void* GetSymbolFromModule(const std::string& module_path, const std::string& symbol_name) {
        std::shared_lock lock(mutex_);
        auto it = loaded_.find(module_path);
        if (it == loaded_.end() || !it->second.handle) return nullptr;
        return GetSymbol(it->second.handle, symbol_name);
    }

    // Can load check
    bool CanLoad(const std::string& path) const {
        return path.ends_with(".node") ||
               path.ends_with(".dll") ||
               path.ends_with(".so") ||
               path.ends_with(".dylib");
    }

    std::string GetName() const { return "NativeModuleLoader"; }

private:
    Options options_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, NativeModuleInfo> loaded_;

    std::string ResolvePath(const std::string& module_path) {
        namespace fs = std::filesystem;

        // Check if it's an absolute path
        if (fs::path(module_path).is_absolute()) {
            if (options_.allow_absolute_paths && fs::exists(module_path)) {
                return module_path;
            }
            return "";
        }

        // Search in configured paths
        for (const auto& search_path : options_.search_paths) {
            fs::path candidate = fs::path(search_path) / module_path;
            if (fs::exists(candidate)) {
                return candidate.string();
            }

            // Try with platform extension
            fs::path with_ext = candidate;
            with_ext.replace_extension(NATIVE_EXT);
            if (fs::exists(with_ext)) {
                return with_ext.string();
            }
        }

        // Try current directory
        if (fs::exists(module_path)) {
            return fs::absolute(module_path).string();
        }

        return "";
    }

    bool IsAllowed(const std::string& module_path) const {
        // Check blocked list first
        for (const auto& blocked : options_.blocked_modules) {
            if (module_path.find(blocked) != std::string::npos) {
                return false;
            }
        }

        // If whitelist is specified, check it
        if (!options_.allowed_modules.empty()) {
            for (const auto& allowed : options_.allowed_modules) {
                if (module_path.find(allowed) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        return true;
    }

    // Platform-specific loading
    static NativeHandle LoadLibraryPlatform(const std::string& path) {
#ifdef _WIN32
        return LoadLibraryA(path.c_str());
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }

    static void UnloadLibraryPlatform(NativeHandle handle) {
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }

    static void* GetSymbol(NativeHandle handle, const std::string& name) {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(handle, name.c_str()));
#else
        return dlsym(handle, name.c_str());
#endif
    }

    static std::string GetLastErrorString() {
#ifdef _WIN32
        DWORD error = GetLastError();
        if (error == 0) return "";
        LPSTR buf = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      nullptr, error, 0, (LPSTR)&buf, 0, nullptr);
        std::string msg = buf ? buf : "";
        LocalFree(buf);
        return msg;
#else
        const char* err = dlerror();
        return err ? err : "";
#endif
    }
};

// Native module resolver
class NativeResolver : public ModuleResolver {
public:
    explicit NativeResolver(std::shared_ptr<NativeModuleLoader> loader = nullptr)
        : loader_(std::move(loader)) {}

    std::optional<ResolveResult> Resolve(
        const std::string& specifier,
        const std::string& parent_path
    ) override {
        if (!specifier.starts_with("native:")) return std::nullopt;

        std::string module_name = specifier.substr(7);  // Remove "native:"

        // Search for the native module
        namespace fs = std::filesystem;
        std::vector<std::string> extensions = {".node", ".dll", ".so", ".dylib"};

        // Try parent directory first
        if (!parent_path.empty()) {
            fs::path parent_dir = fs::path(parent_path).parent_path();
            for (const auto& ext : extensions) {
                fs::path candidate = parent_dir / (module_name + ext);
                if (fs::exists(candidate)) {
                    return ResolveResult{candidate.string(), GetName(), false};
                }
            }
        }

        // Try node_modules patterns
        std::vector<fs::path> search_dirs;
        if (!parent_path.empty()) {
            fs::path current = fs::path(parent_path).parent_path();
            while (current.has_parent_path()) {
                search_dirs.push_back(current / "node_modules" / module_name / "build" / "Release");
                search_dirs.push_back(current / "node_modules" / module_name / "prebuilds");
                current = current.parent_path();
            }
        }

        for (const auto& dir : search_dirs) {
            for (const auto& ext : extensions) {
                fs::path candidate = dir / (module_name + ext);
                if (fs::exists(candidate)) {
                    return ResolveResult{candidate.string(), GetName(), false};
                }
            }
        }

        // Return the bare specifier for the loader to handle
        return ResolveResult{module_name, GetName(), false};
    }

    bool CanHandle(const std::string& specifier) const override {
        return specifier.starts_with("native:");
    }

    std::string GetName() const override { return "NativeResolver"; }

    void SetLoader(std::shared_ptr<NativeModuleLoader> loader) {
        loader_ = std::move(loader);
    }

private:
    std::shared_ptr<NativeModuleLoader> loader_;
};

// Registry for tracking native modules across the application
class NativeModuleRegistry {
public:
    static NativeModuleRegistry& Instance() {
        static NativeModuleRegistry instance;
        return instance;
    }

    void Register(const std::string& name, NativeModuleInfo info) {
        std::lock_guard lock(mutex_);
        registry_[name] = std::move(info);
    }

    void Unregister(const std::string& name) {
        std::lock_guard lock(mutex_);
        registry_.erase(name);
    }

    std::optional<NativeModuleInfo> Get(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = registry_.find(name);
        if (it != registry_.end()) return it->second;
        return std::nullopt;
    }

    std::vector<std::string> GetAll() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        for (const auto& [name, _] : registry_) {
            result.push_back(name);
        }
        return result;
    }

    size_t Count() const {
        std::shared_lock lock(mutex_);
        return registry_.size();
    }

    void Clear() {
        std::lock_guard lock(mutex_);
        registry_.clear();
    }

private:
    NativeModuleRegistry() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, NativeModuleInfo> registry_;
};

//=============================================================================
// Loader Chain - Combines multiple loaders
//=============================================================================

class LoaderChain : public ModuleLoader {
public:
    LoaderChain() = default;
    
    explicit LoaderChain(std::vector<ModuleLoaderPtr> loaders) 
        : loaders_(std::move(loaders)) {}
    
    void AddLoader(ModuleLoaderPtr loader) {
        loaders_.push_back(std::move(loader));
    }
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        for (const auto& loader : loaders_) {
            if (loader->CanLoad(resolved_path)) {
                auto result = loader->Load(resolved_path);
                if (result) {
                    LOG_DEBUG("LoaderChain",
                        loader->GetName() + " loaded: " + resolved_path);
                    return result;
                }
            }
        }
        return std::nullopt;
    }

    bool CanLoad(const std::string& resolved_path) const override {
        for (const auto& loader : loaders_) {
            if (loader->CanLoad(resolved_path)) return true;
        }
        return false;
    }

    std::string GetName() const override { return "LoaderChain"; }

    std::vector<std::string> GetLoaderNames() const {
        std::vector<std::string> names;
        for (const auto& loader : loaders_) {
            names.push_back(loader->GetName());
        }
        return names;
    }

private:
    std::vector<ModuleLoaderPtr> loaders_;
};

//=============================================================================
// Transforming Loader - Wraps a loader and applies transformations
//=============================================================================

class TransformingLoader : public ModuleLoader {
public:
    TransformingLoader(ModuleLoaderPtr inner_loader, ModuleTransformerPtr transformer)
        : inner_loader_(std::move(inner_loader))
        , transformer_(std::move(transformer)) {}

    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        // First, load the module using the inner loader
        auto module_info = inner_loader_->Load(resolved_path);
        if (!module_info) return std::nullopt;

        // Check if we should transform this module
        if (transformer_ && transformer_->ShouldTransform(resolved_path)) {
            auto transform_result = transformer_->Transform(
                module_info->source,
                resolved_path,
                module_info->format
            );

            if (transform_result.is_ok()) {
                // Store original and apply transformation
                module_info->original_source = module_info->source;
                module_info->source = transform_result.code;
                module_info->transformed = true;

                // Store source map if available
                if (transform_result.source_map) {
                    module_info->source_map = transform_result.source_map;
                }

                // Update format if TypeScript was transformed to JavaScript
                if (resolved_path.ends_with(".ts") ||
                    resolved_path.ends_with(".tsx") ||
                    resolved_path.ends_with(".mts") ||
                    resolved_path.ends_with(".cts")) {
                    // After transformation, treat as JS
                    if (resolved_path.ends_with(".mts")) {
                        module_info->format = ModuleFormat::ESModule;
                    } else if (resolved_path.ends_with(".cts")) {
                        module_info->format = ModuleFormat::CommonJS;
                    }
                }

                LOG_DEBUG("TransformingLoader", "Transformed: " + resolved_path);
            } else {
                // Log transformation errors
                for (const auto& error : transform_result.errors) {
                    LOG_ERROR("TransformingLoader", "Transform error: " + error);
                }
                // Return original code on error (or could return nullopt)
            }
        }

        return module_info;
    }

    bool CanLoad(const std::string& resolved_path) const override {
        return inner_loader_->CanLoad(resolved_path);
    }

    std::string GetName() const override {
        return "TransformingLoader(" + inner_loader_->GetName() + ")";
    }

    // Set/replace transformer
    void SetTransformer(ModuleTransformerPtr transformer) {
        transformer_ = std::move(transformer);
    }

    // Add to transformer chain (creates chain if needed)
    void AddTransformer(ModuleTransformerPtr transformer) {
        if (!transformer_) {
            transformer_ = std::move(transformer);
        } else {
            // Check if already a chain
            auto chain = std::dynamic_pointer_cast<TransformerChain>(transformer_);
            if (chain) {
                chain->AddTransformer(std::move(transformer));
            } else {
                // Create a new chain with existing + new
                auto new_chain = std::make_shared<TransformerChain>();
                new_chain->AddTransformer(std::move(transformer_));
                new_chain->AddTransformer(std::move(transformer));
                transformer_ = new_chain;
            }
        }
    }

    // Configure source map behavior
    void SetInlineSourceMaps(bool inline_) { inline_source_maps_ = inline_; }

    // Get the inner loader
    ModuleLoaderPtr GetInnerLoader() const { return inner_loader_; }

private:
    ModuleLoaderPtr inner_loader_;
    ModuleTransformerPtr transformer_;
    bool inline_source_maps_{true};
};

} // namespace experiments