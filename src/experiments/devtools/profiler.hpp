#pragma once

/**
 * profiler.hpp - Performance Profiler
 */

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <limits>
#include <type_traits>

namespace experiments {

struct ProfilerEntry {
    std::string name;
    std::string category;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::chrono::microseconds duration{0};
    size_t memory_before{0};
    size_t memory_after{0};
    size_t call_count{1};
    std::unordered_map<std::string, std::string> metadata;
    
    double DurationMs() const {
        return static_cast<double>(duration.count()) / 1000.0;
    }
    
    std::string ToString() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "[" << category << "] " << name << ": "
            << DurationMs() << "ms";
        if (call_count > 1) {
            oss << " (" << call_count << " calls, avg "
                << (DurationMs() / call_count) << "ms)";
        }
        return oss.str();
    }
};

class Profiler {
public:
    struct Options {
        bool enabled{true};
        bool track_memory{true};
        bool track_cpu{true};
        size_t max_entries{10000};
        std::chrono::microseconds min_duration{0};
    };
    
    struct Stats {
        size_t total_entries{0};
        double total_time_ms{0};
        double avg_time_ms{0};
        double min_time_ms{0};
        double max_time_ms{0};
    };
    
    explicit Profiler(Options options = {}) : options_(std::move(options)) {}
    
    void Begin(const std::string& name, const std::string& category = "default") {
        if (!options_.enabled) return;
        
        std::lock_guard lock(mutex_);
        ProfilerEntry entry;
        entry.name = name;
        entry.category = category;
        entry.start = std::chrono::steady_clock::now();
        if (options_.track_memory) {
            entry.memory_before = GetMemoryUsage();
        }
        
        active_[name] = entry;
    }
    
    void End(const std::string& name) {
        if (!options_.enabled) return;
        
        auto end_time = std::chrono::steady_clock::now();
        
        std::lock_guard lock(mutex_);
        auto it = active_.find(name);
        if (it == active_.end()) return;
        
        ProfilerEntry entry = std::move(it->second);
        active_.erase(it);
        
        entry.end = end_time;
        entry.duration = std::chrono::duration_cast<std::chrono::microseconds>(
            entry.end - entry.start);
        
        if (options_.track_memory) {
            entry.memory_after = GetMemoryUsage();
        }
        
        if (entry.duration >= options_.min_duration) {
            AddEntry(std::move(entry));
        }
    }
    
    void Mark(const std::string& name, const std::string& category = "marker") {
        if (!options_.enabled) return;
        
        ProfilerEntry entry;
        entry.name = name;
        entry.category = category;
        entry.start = std::chrono::steady_clock::now();
        entry.end = entry.start;
        entry.duration = std::chrono::microseconds(0);
        
        std::lock_guard lock(mutex_);
        AddEntry(std::move(entry));
    }
    
    template<typename F>
    auto Measure(const std::string& name, F&& func) 
        -> decltype(std::forward<F>(func)()) {
        Begin(name);
        if constexpr (std::is_void_v<decltype(func())>) {
            std::forward<F>(func)();
            End(name);
        } else {
            auto result = std::forward<F>(func)();
            End(name);
            return result;
        }
    }
    
    std::vector<ProfilerEntry> GetEntries(const std::string& category = "") const {
        std::lock_guard lock(mutex_);
        
        if (category.empty()) {
            return std::vector<ProfilerEntry>(entries_.begin(), entries_.end());
        }
        
        std::vector<ProfilerEntry> filtered;
        for (const auto& entry : entries_) {
            if (entry.category == category) {
                filtered.push_back(entry);
            }
        }
        return filtered;
    }
    
    Stats GetStats(const std::string& name = "") const {
        std::lock_guard lock(mutex_);
        
        Stats stats;
        double min = std::numeric_limits<double>::max();
        double max = 0;
        
        for (const auto& entry : entries_) {
            if (name.empty() || entry.name == name) {
                double ms = entry.DurationMs();
                stats.total_time_ms += ms;
                stats.total_entries++;
                min = std::min(min, ms);
                max = std::max(max, ms);
            }
        }
        
        if (stats.total_entries > 0) {
            stats.avg_time_ms = stats.total_time_ms / stats.total_entries;
            stats.min_time_ms = min;
            stats.max_time_ms = max;
        }
        
        return stats;
    }
    
    std::string GenerateReport() const {
        std::lock_guard lock(mutex_);
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "=== Profiler Report ===\n";
        oss << "Total entries: " << entries_.size() << "\n\n";
        
        std::unordered_map<std::string, std::vector<const ProfilerEntry*>> by_category;
        for (const auto& entry : entries_) {
            by_category[entry.category].push_back(&entry);
        }
        
        for (const auto& [cat, entries] : by_category) {
            oss << "[" << cat << "]\n";
            double total = 0;
            for (const auto* e : entries) {
                oss << "  " << e->name << ": " << e->DurationMs() << "ms\n";
                total += e->DurationMs();
            }
            oss << "  Total: " << total << "ms\n\n";
        }
        
        return oss.str();
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
        active_.clear();
    }
    
    Options& GetOptions() { return options_; }
    bool IsEnabled() const { return options_.enabled; }
    void Enable() { options_.enabled = true; }
    void Disable() { options_.enabled = false; }
    
private:
    Options options_;
    mutable std::mutex mutex_;
    std::deque<ProfilerEntry> entries_;
    std::unordered_map<std::string, ProfilerEntry> active_;
    
    void AddEntry(ProfilerEntry entry) {
        if (entries_.size() >= options_.max_entries) {
            entries_.pop_front();
        }
        entries_.push_back(std::move(entry));
    }
    
    static size_t GetMemoryUsage() {
        return 0;  // Platform-specific
    }
};

// RAII helper for profiling
class ProfilingScope {
public:
    ProfilingScope(Profiler& profiler, const std::string& name, 
                   const std::string& category = "default")
        : profiler_(profiler), name_(name) {
        profiler_.Begin(name, category);
    }
    
    ~ProfilingScope() {
        profiler_.End(name_);
    }
    
    ProfilingScope(const ProfilingScope&) = delete;
    ProfilingScope& operator=(const ProfilingScope&) = delete;
    
private:
    Profiler& profiler_;
    std::string name_;
};

#define PROFILE_SCOPE(profiler, name) ProfilingScope _ps_##__LINE__(profiler, name)
#define PROFILE_FUNCTION(profiler) ProfilingScope _ps_fn_(profiler, __FUNCTION__)

} // namespace experiments
