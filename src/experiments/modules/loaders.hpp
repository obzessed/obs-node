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

namespace experiments {

//=============================================================================
// Module Loader - Abstract base class for loading module content
//=============================================================================

class ModuleLoader {
public:
    virtual ~ModuleLoader() = default;
    
    virtual std::optional<ModuleInfo> Load(const std::string& resolved_path) = 0;
    virtual bool CanLoad(const std::string& resolved_path) const = 0;
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
    
    void Register(const std::string& name, const std::string& source, 
                  ModuleFormat format = ModuleFormat::CommonJS) {
        std::lock_guard lock(mutex_);
        modules_[name] = {name, prefix_ + name, source, "", format, false, std::nullopt};
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
        
        auto source = FetchUrl(resolved_path);
        if (!source) return std::nullopt;
        
        ModuleInfo info;
        info.resolved_path = resolved_path;
        info.source = *source;
        info.format = ModuleUtils::DetectFormatFromPath(resolved_path);
        if (info.format == ModuleFormat::Unknown) {
            info.format = ModuleUtils::DetectFormatFromContent(info.source);
        }
        
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
        LOG_INFO("RemoteLoader", "Would fetch: " + url);
        return std::nullopt;
    }
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
    
    void AddLoaderFirst(ModuleLoaderPtr loader) {
        loaders_.insert(loaders_.begin(), std::move(loader));
    }
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        for (const auto& loader : loaders_) {
            if (loader->CanLoad(resolved_path)) {
                auto result = loader->Load(resolved_path);
                if (result) {
                    LOG_DEBUG("LoaderChain", loader->GetName() + " loaded: " + resolved_path);
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

} // namespace experiments
