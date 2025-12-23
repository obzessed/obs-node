#pragma once

/**
 * cache.hpp - Module Cache and Dependency Graph
 */

#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <chrono>
#include <list>
#include <vector>
#include <algorithm>

#include "module_info.hpp"
#include "loaders.hpp"
#include "../core/logger.hpp"

namespace experiments {

//=============================================================================
// Module Cache Entry
//=============================================================================

struct ModuleCacheEntry {
    ModuleInfo module;
    std::chrono::steady_clock::time_point loaded_at;
    std::chrono::steady_clock::time_point last_accessed;
    size_t access_count{0};
    size_t size_bytes{0};
    
    ModuleCacheEntry() = default;
    explicit ModuleCacheEntry(ModuleInfo mod) 
        : module(std::move(mod))
        , loaded_at(std::chrono::steady_clock::now())
        , last_accessed(loaded_at)
        , access_count(1) {
        size_bytes = module.source.size() + module.original_source.size() + 
                     module.resolved_path.size() + module.specifier.size();
    }
    
    void Touch() {
        last_accessed = std::chrono::steady_clock::now();
        ++access_count;
    }
    
    std::chrono::milliseconds Age() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loaded_at);
    }
};

//=============================================================================
// Module Cache - LRU cache for loaded modules
//=============================================================================

class ModuleCache {
public:
    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t total_size_bytes{0};
        size_t entry_count{0};
        
        double HitRate() const {
            size_t total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };
    
    struct Options {
        size_t max_entries{1000};
        size_t max_size_bytes{100 * 1024 * 1024};
        std::chrono::seconds max_age{3600};
        bool enable_lru{true};
    };
    
    explicit ModuleCache(Options options = {}) : options_(std::move(options)) {}
    
    std::optional<ModuleInfo> Get(const std::string& resolved_path) {
        std::lock_guard lock(mutex_);
        
        auto it = cache_.find(resolved_path);
        if (it == cache_.end()) {
            ++stats_.misses;
            return std::nullopt;
        }
        
        if (it->second.Age() > options_.max_age) {
            cache_.erase(it);
            lru_order_.remove(resolved_path);
            --stats_.entry_count;
            ++stats_.misses;
            return std::nullopt;
        }
        
        it->second.Touch();
        
        if (options_.enable_lru) {
            lru_order_.remove(resolved_path);
            lru_order_.push_front(resolved_path);
        }
        
        ++stats_.hits;
        return it->second.module;
    }
    
    void Put(const std::string& resolved_path, const ModuleInfo& module) {
        std::lock_guard lock(mutex_);
        
        ModuleCacheEntry entry(module);
        
        while (ShouldEvict(entry.size_bytes)) {
            EvictOne();
        }
        
        auto existing = cache_.find(resolved_path);
        if (existing != cache_.end()) {
            stats_.total_size_bytes -= existing->second.size_bytes;
            lru_order_.remove(resolved_path);
        }
        
        cache_[resolved_path] = std::move(entry);
        stats_.total_size_bytes += cache_[resolved_path].size_bytes;
        stats_.entry_count = cache_.size();
        
        if (options_.enable_lru) {
            lru_order_.push_front(resolved_path);
        }
    }
    
    bool Has(const std::string& resolved_path) const {
        std::shared_lock lock(mutex_);
        return cache_.contains(resolved_path);
    }
    
    void Invalidate(const std::string& resolved_path) {
        std::lock_guard lock(mutex_);
        auto it = cache_.find(resolved_path);
        if (it != cache_.end()) {
            stats_.total_size_bytes -= it->second.size_bytes;
            cache_.erase(it);
            lru_order_.remove(resolved_path);
            stats_.entry_count = cache_.size();
        }
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
        lru_order_.clear();
        stats_.total_size_bytes = 0;
        stats_.entry_count = 0;
    }
    
    Stats GetStats() const {
        std::shared_lock lock(mutex_);
        return stats_;
    }
    
private:
    Options options_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ModuleCacheEntry> cache_;
    std::list<std::string> lru_order_;
    Stats stats_;
    
    bool ShouldEvict(size_t additional_bytes) const {
        return cache_.size() >= options_.max_entries ||
               stats_.total_size_bytes + additional_bytes > options_.max_size_bytes;
    }
    
    void EvictOne() {
        if (lru_order_.empty()) return;
        
        const std::string& to_evict = lru_order_.back();
        auto it = cache_.find(to_evict);
        if (it != cache_.end()) {
            stats_.total_size_bytes -= it->second.size_bytes;
            cache_.erase(it);
            ++stats_.evictions;
        }
        lru_order_.pop_back();
    }
};

//=============================================================================
// Dependency Node
//=============================================================================

struct DependencyNode {
    std::string module_path;
    std::unordered_set<std::string> dependencies;
    std::unordered_set<std::string> dependents;
    std::chrono::steady_clock::time_point discovered;
    
    DependencyNode() : discovered(std::chrono::steady_clock::now()) {}
    explicit DependencyNode(std::string path) 
        : module_path(std::move(path))
        , discovered(std::chrono::steady_clock::now()) {}
};

//=============================================================================
// Dependency Graph - Tracks module dependencies
//=============================================================================

class DependencyGraph {
public:
    enum class CycleAction {
        Ignore,
        Warn,
        Error
    };
    
    DependencyGraph(CycleAction cycle_action = CycleAction::Warn)
        : cycle_action_(cycle_action) {}
    
    void AddDependency(const std::string& module, const std::string& dependency) {
        std::lock_guard lock(mutex_);
        
        if (!nodes_.contains(module)) {
            nodes_[module] = DependencyNode(module);
        }
        if (!nodes_.contains(dependency)) {
            nodes_[dependency] = DependencyNode(dependency);
        }
        
        nodes_[module].dependencies.insert(dependency);
        nodes_[dependency].dependents.insert(module);
    }
    
    bool WouldCreateCycle(const std::string& module, const std::string& dependency) const {
        std::shared_lock lock(mutex_);
        std::unordered_set<std::string> visited;
        return HasPath(dependency, module, visited);
    }
    
    std::vector<std::string> GetAffectedModules(const std::string& changed_module) const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> affected;
        std::unordered_set<std::string> visited;
        CollectDependents(changed_module, visited, affected);
        return affected;
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        nodes_.clear();
    }
    
    size_t Size() const {
        std::shared_lock lock(mutex_);
        return nodes_.size();
    }
    
    CycleAction GetCycleAction() const { return cycle_action_; }
    void SetCycleAction(CycleAction action) { cycle_action_ = action; }

private:
    CycleAction cycle_action_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, DependencyNode> nodes_;
    
    bool HasPath(const std::string& from, const std::string& to, 
                 std::unordered_set<std::string>& visited) const {
        if (from == to) return true;
        if (visited.contains(from)) return false;
        
        visited.insert(from);
        
        auto it = nodes_.find(from);
        if (it == nodes_.end()) return false;
        
        for (const auto& dep : it->second.dependencies) {
            if (HasPath(dep, to, visited)) return true;
        }
        return false;
    }
    
    void CollectDependents(const std::string& module,
                          std::unordered_set<std::string>& visited,
                          std::vector<std::string>& result) const {
        auto it = nodes_.find(module);
        if (it == nodes_.end()) return;
        
        for (const auto& dependent : it->second.dependents) {
            if (!visited.contains(dependent)) {
                visited.insert(dependent);
                result.push_back(dependent);
                CollectDependents(dependent, visited, result);
            }
        }
    }
};

//=============================================================================
// Caching Loader - Wraps a loader with caching
//=============================================================================

class CachingLoader : public ModuleLoader {
public:
    CachingLoader(ModuleLoaderPtr inner_loader, 
                  std::shared_ptr<ModuleCache> cache,
                  std::shared_ptr<DependencyGraph> graph = nullptr)
        : inner_loader_(std::move(inner_loader))
        , cache_(std::move(cache))
        , graph_(std::move(graph)) {}
    
    std::optional<ModuleInfo> Load(const std::string& resolved_path) override {
        if (auto cached = cache_->Get(resolved_path)) {
            LOG_DEBUG("CachingLoader", "Cache hit: " + resolved_path);
            return cached;
        }
        
        auto module = inner_loader_->Load(resolved_path);
        if (module) {
            cache_->Put(resolved_path, *module);
            LOG_DEBUG("CachingLoader", "Cached: " + resolved_path);
        }
        
        return module;
    }
    
    bool CanLoad(const std::string& resolved_path) const override {
        return inner_loader_->CanLoad(resolved_path);
    }
    
    std::string GetName() const override {
        return "CachingLoader(" + inner_loader_->GetName() + ")";
    }
    
    void TrackDependency(const std::string& importer, const std::string& imported) {
        if (graph_) {
            if (graph_->GetCycleAction() != DependencyGraph::CycleAction::Ignore) {
                if (graph_->WouldCreateCycle(importer, imported)) {
                    if (graph_->GetCycleAction() == DependencyGraph::CycleAction::Warn) {
                        LOG_WARN("CachingLoader", "Circular dependency detected: " + 
                                importer + " -> " + imported);
                    }
                }
            }
            graph_->AddDependency(importer, imported);
        }
    }
    
    void InvalidateWithDependents(const std::string& resolved_path) {
        cache_->Invalidate(resolved_path);
        
        if (graph_) {
            for (const auto& affected : graph_->GetAffectedModules(resolved_path)) {
                cache_->Invalidate(affected);
            }
        }
    }
    
    ModuleCache::Stats GetCacheStats() const {
        return cache_->GetStats();
    }
    
private:
    ModuleLoaderPtr inner_loader_;
    std::shared_ptr<ModuleCache> cache_;
    std::shared_ptr<DependencyGraph> graph_;
};

} // namespace experiments
