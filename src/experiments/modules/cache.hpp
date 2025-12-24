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
#include <future>

#include "module_info.hpp"
#include "loaders.hpp"
#include "../core/logger.hpp"

namespace experiments {

//=============================================================================
// Module Cache - LRU cache for loaded modules
//=============================================================================

struct ModuleCacheEntry {
    ModuleInfo module;
    std::chrono::steady_clock::time_point loaded_at;
    std::chrono::steady_clock::time_point last_accessed;
    size_t access_count{0};
    size_t size_bytes{0};  // Approximate size in memory

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
        size_t max_size_bytes{100 * 1024 * 1024};  // 100 MB
        std::chrono::seconds max_age{3600};         // 1 hour
        bool enable_lru{true};
    };

    explicit ModuleCache(Options options = {}) : options_(std::move(options)) {}

    // Get a cached module
    std::optional<ModuleInfo> Get(const std::string& resolved_path) {
        std::lock_guard lock(mutex_);

        auto it = cache_.find(resolved_path);
        if (it == cache_.end()) {
            ++stats_.misses;
            return std::nullopt;
        }

        // Check if expired
        if (it->second.Age() > options_.max_age) {
            cache_.erase(it);
            lru_order_.remove(resolved_path);
            --stats_.entry_count;
            ++stats_.misses;
            return std::nullopt;
        }

        it->second.Touch();

        // Move to front of LRU
        if (options_.enable_lru) {
            lru_order_.remove(resolved_path);
            lru_order_.push_front(resolved_path);
        }

        ++stats_.hits;
        return it->second.module;
    }

    // Store a module in the cache
    void Put(const std::string& resolved_path, const ModuleInfo& module) {
        std::lock_guard lock(mutex_);

        ModuleCacheEntry entry(module);

        // Evict if necessary
        while (ShouldEvict(entry.size_bytes)) {
            EvictOne();
        }

        // Remove if already exists (update)
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

    // Check if a module is cached
    bool Has(const std::string& resolved_path) const {
        std::shared_lock lock(mutex_);
        return cache_.contains(resolved_path);
    }

    // Invalidate a specific entry
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

    // Invalidate entries matching a pattern
    void InvalidatePattern(const std::string& pattern) {
        std::lock_guard lock(mutex_);
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (it->first.find(pattern) != std::string::npos) {
                stats_.total_size_bytes -= it->second.size_bytes;
                lru_order_.remove(it->first);
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
        stats_.entry_count = cache_.size();
    }

    // Clear all entries
    void Clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
        lru_order_.clear();
        stats_.total_size_bytes = 0;
        stats_.entry_count = 0;
    }

    // Get stats
    Stats GetStats() const {
        std::shared_lock lock(mutex_);
        return stats_;
    }

    // Get all cached paths
    std::vector<std::string> GetCachedPaths() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> paths;
        paths.reserve(cache_.size());
        for (const auto& [path, _] : cache_) {
            paths.push_back(path);
        }
        return paths;
    }

private:
    Options options_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ModuleCacheEntry> cache_;
    std::list<std::string> lru_order_;  // Front = most recent
    Stats stats_;

    bool ShouldEvict(size_t additional_bytes) const {
        return cache_.size() >= options_.max_entries ||
               stats_.total_size_bytes + additional_bytes > options_.max_size_bytes;
    }

    void EvictOne() {
        if (lru_order_.empty()) return;

        // Evict least recently used (back of list)
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
// Dependency Graph - Tracks module dependencies
//=============================================================================

struct DependencyNode {
    std::string module_path;
    std::unordered_set<std::string> dependencies;    // Modules this depends on
    std::unordered_set<std::string> dependents;       // Modules that depend on this
    std::chrono::steady_clock::time_point discovered;

    DependencyNode() : discovered(std::chrono::steady_clock::now()) {}
    explicit DependencyNode(std::string path)
        : module_path(std::move(path))
        , discovered(std::chrono::steady_clock::now()) {}
};

class DependencyGraph {
public:
    enum class CycleAction {
        Ignore,     // Allow cycles (may cause infinite loops)
        Warn,       // Log warning but continue
        Error       // Return error, abort load
    };

    DependencyGraph(CycleAction cycle_action = CycleAction::Warn)
        : cycle_action_(cycle_action) {}

    // Add a dependency relationship
    void AddDependency(const std::string& module, const std::string& dependency) {
        std::lock_guard lock(mutex_);

        // Ensure both nodes exist
        if (!nodes_.contains(module)) {
            nodes_[module] = DependencyNode(module);
        }
        if (!nodes_.contains(dependency)) {
            nodes_[dependency] = DependencyNode(dependency);
        }

        nodes_[module].dependencies.insert(dependency);
        nodes_[dependency].dependents.insert(module);
    }

    // Remove a module and all its relationships
    void RemoveModule(const std::string& module) {
        std::lock_guard lock(mutex_);

        auto it = nodes_.find(module);
        if (it == nodes_.end()) return;

        // Remove from dependents' dependency lists
        for (const auto& dep : it->second.dependencies) {
            if (auto dep_it = nodes_.find(dep); dep_it != nodes_.end()) {
                dep_it->second.dependents.erase(module);
            }
        }

        // Remove from dependencies' dependent lists
        for (const auto& dependent : it->second.dependents) {
            if (auto dep_it = nodes_.find(dependent); dep_it != nodes_.end()) {
                dep_it->second.dependencies.erase(module);
            }
        }

        nodes_.erase(it);
    }

    // Check for circular dependency before adding
    bool WouldCreateCycle(const std::string& module, const std::string& dependency) const {
        std::shared_lock lock(mutex_);

        // If dependency directly or indirectly depends on module, adding this creates a cycle
        std::unordered_set<std::string> visited;
        return HasPath(dependency, module, visited);
    }

    // Detect if a cycle exists involving a module
    std::optional<std::vector<std::string>> DetectCycle(const std::string& start) const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> path;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> in_stack;

        if (DetectCycleHelper(start, visited, in_stack, path)) {
            return path;
        }
        return std::nullopt;
    }

    // Get all dependencies of a module (transitive)
    std::vector<std::string> GetAllDependencies(const std::string& module) const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> result;
        std::unordered_set<std::string> visited;
        CollectDependencies(module, visited, result);
        return result;
    }

    // Get all dependents of a module (what depends on it)
    std::vector<std::string> GetDependents(const std::string& module) const {
        std::shared_lock lock(mutex_);

        auto it = nodes_.find(module);
        if (it == nodes_.end()) return {};

        return std::vector<std::string>(
            it->second.dependents.begin(),
            it->second.dependents.end()
        );
    }

    // Get modules affected by a change (dependents, transitively)
    std::vector<std::string> GetAffectedModules(const std::string& changed_module) const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> affected;
        std::unordered_set<std::string> visited;
        CollectDependents(changed_module, visited, affected);
        return affected;
    }

    // Get topological order (dependencies first)
    std::vector<std::string> GetTopologicalOrder() const {
        std::shared_lock lock(mutex_);

        std::vector<std::string> order;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> in_stack;

        for (const auto& [module, _] : nodes_) {
            if (!visited.contains(module)) {
                TopologicalSort(module, visited, in_stack, order);
            }
        }

        std::reverse(order.begin(), order.end());
        return order;
    }

    // Clear the graph
    void Clear() {
        std::lock_guard lock(mutex_);
        nodes_.clear();
    }

    // Get module count
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

    bool DetectCycleHelper(const std::string& node,
                          std::unordered_set<std::string>& visited,
                          std::unordered_set<std::string>& in_stack,
                          std::vector<std::string>& path) const {
        visited.insert(node);
        in_stack.insert(node);
        path.push_back(node);

        auto it = nodes_.find(node);
        if (it != nodes_.end()) {
            for (const auto& dep : it->second.dependencies) {
                if (!visited.contains(dep)) {
                    if (DetectCycleHelper(dep, visited, in_stack, path)) {
                        return true;
                    }
                } else if (in_stack.contains(dep)) {
                    path.push_back(dep);  // Complete the cycle
                    return true;
                }
            }
        }

        path.pop_back();
        in_stack.erase(node);
        return false;
    }

    void CollectDependencies(const std::string& module,
                            std::unordered_set<std::string>& visited,
                            std::vector<std::string>& result) const {
        auto it = nodes_.find(module);
        if (it == nodes_.end()) return;

        for (const auto& dep : it->second.dependencies) {
            if (!visited.contains(dep)) {
                visited.insert(dep);
                result.push_back(dep);
                CollectDependencies(dep, visited, result);
            }
        }
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

    void TopologicalSort(const std::string& node,
                        std::unordered_set<std::string>& visited,
                        std::unordered_set<std::string>& in_stack,
                        std::vector<std::string>& order) const {
        visited.insert(node);
        in_stack.insert(node);

        auto it = nodes_.find(node);
        if (it != nodes_.end()) {
            for (const auto& dep : it->second.dependencies) {
                if (!visited.contains(dep)) {
                    TopologicalSort(dep, visited, in_stack, order);
                }
            }
        }

        in_stack.erase(node);
        order.push_back(node);
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
        // Check cache first
        if (auto cached = cache_->Get(resolved_path)) {
            LOG_DEBUG("CachingLoader", "Cache hit: " + resolved_path);
            return cached;
        }

        // Load from inner loader
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

    // Track dependency when one module imports another
    void TrackDependency(const std::string& importer, const std::string& imported) {
        if (graph_) {
            if (graph_->GetCycleAction() != DependencyGraph::CycleAction::Ignore) {
                if (graph_->WouldCreateCycle(importer, imported)) {
                    if (graph_->GetCycleAction() == DependencyGraph::CycleAction::Warn) {
                        LOG_WARN("CachingLoader", "Circular dependency detected: " +
                                importer + " -> " + imported);
                    }
                    // For Error action, caller should check WouldCreateCycle first
                }
            }
            graph_->AddDependency(importer, imported);
        }
    }

    // Invalidate a module and all its dependents
    void InvalidateWithDependents(const std::string& resolved_path) {
        cache_->Invalidate(resolved_path);

        if (graph_) {
            for (const auto& affected : graph_->GetAffectedModules(resolved_path)) {
                cache_->Invalidate(affected);
            }
        }
    }

    // Get cache stats
    ModuleCache::Stats GetCacheStats() const {
        return cache_->GetStats();
    }

    // Get dependency graph
    std::shared_ptr<DependencyGraph> GetDependencyGraph() const {
        return graph_;
    }

    // Get inner loader
    ModuleLoaderPtr GetInnerLoader() const {
        return inner_loader_;
    }
    
    // Async preloading for cache warming
    std::future<bool> PreloadAsync(const std::string& resolved_path) {
        return std::async(std::launch::async, [this, resolved_path]() {
            auto result = Load(resolved_path);
            return result.has_value();
        });
    }
    
    // Batch async preloading
    std::vector<std::future<bool>> PreloadBatchAsync(const std::vector<std::string>& paths) {
        std::vector<std::future<bool>> futures;
        futures.reserve(paths.size());
        for (const auto& path : paths) {
            futures.push_back(PreloadAsync(path));
        }
        return futures;
    }
    
    // Synchronous batch preload with progress callback
    using ProgressCallback = std::function<void(size_t current, size_t total, const std::string& path)>;
    
    size_t PreloadBatch(const std::vector<std::string>& paths, ProgressCallback progress = nullptr) {
        size_t loaded = 0;
        for (size_t i = 0; i < paths.size(); ++i) {
            if (progress) progress(i + 1, paths.size(), paths[i]);
            if (Load(paths[i])) loaded++;
        }
        return loaded;
    }
    
private:
    ModuleLoaderPtr inner_loader_;
    std::shared_ptr<ModuleCache> cache_;
    std::shared_ptr<DependencyGraph> graph_;
};

} // namespace experiments
