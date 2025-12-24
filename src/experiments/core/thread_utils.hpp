#pragma once

/**
 * thread_utils.hpp - Thread Safety Utilities
 * 
 * Provides thread-safe wrappers and RAII lock helpers.
 */

#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>
#include <functional>

namespace experiments {

//=============================================================================
// RAII Lock Helpers
//=============================================================================

template<typename Mutex>
class ReadLock {
public:
    explicit ReadLock(Mutex& m) : mutex_(m) { mutex_.lock_shared(); }
    ~ReadLock() { mutex_.unlock_shared(); }
    
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
    
private:
    Mutex& mutex_;
};

template<typename Mutex>
class WriteLock {
public:
    explicit WriteLock(Mutex& m) : mutex_(m) { mutex_.lock(); }
    ~WriteLock() { mutex_.unlock(); }
    
    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;
    
private:
    Mutex& mutex_;
};

//=============================================================================
// ThreadSafeMap - Thread-safe key-value container
//=============================================================================

template<typename K, typename V>
class ThreadSafeMap {
public:
    using MapType = std::unordered_map<K, V>;
    
    ThreadSafeMap() = default;
    
    // Insert or update
    void Set(const K& key, const V& value) {
        std::unique_lock lock(mutex_);
        data_[key] = value;
    }
    
    void Set(const K& key, V&& value) {
        std::unique_lock lock(mutex_);
        data_[key] = std::move(value);
    }
    
    // Get value (returns copy)
    std::optional<V> Get(const K& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) return it->second;
        return std::nullopt;
    }
    
    // Get with default
    V GetOr(const K& key, const V& default_value) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        return it != data_.end() ? it->second : default_value;
    }
    
    // Check existence
    bool Has(const K& key) const {
        std::shared_lock lock(mutex_);
        return data_.find(key) != data_.end();
    }
    
    // Remove
    bool Remove(const K& key) {
        std::unique_lock lock(mutex_);
        return data_.erase(key) > 0;
    }
    
    // Clear all
    void Clear() {
        std::unique_lock lock(mutex_);
        data_.clear();
    }
    
    // Size
    size_t Size() const {
        std::shared_lock lock(mutex_);
        return data_.size();
    }
    
    bool Empty() const {
        std::shared_lock lock(mutex_);
        return data_.empty();
    }
    
    // Get all keys
    std::vector<K> Keys() const {
        std::shared_lock lock(mutex_);
        std::vector<K> keys;
        keys.reserve(data_.size());
        for (const auto& [k, v] : data_) {
            keys.push_back(k);
        }
        return keys;
    }
    
    // Get snapshot (copy of entire map)
    MapType Snapshot() const {
        std::shared_lock lock(mutex_);
        return data_;
    }
    
    // Apply function to value under lock
    template<typename Fn>
    bool Apply(const K& key, Fn&& fn) {
        std::unique_lock lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            fn(it->second);
            return true;
        }
        return false;
    }
    
    // ForEach (read-only)
    template<typename Fn>
    void ForEach(Fn&& fn) const {
        std::shared_lock lock(mutex_);
        for (const auto& [k, v] : data_) {
            fn(k, v);
        }
    }
    
    // Insert if not exists (returns true if inserted)
    bool TryInsert(const K& key, const V& value) {
        std::unique_lock lock(mutex_);
        auto [it, inserted] = data_.try_emplace(key, value);
        return inserted;
    }
    
    // GetOrInsert - get existing or insert new
    V& GetOrInsert(const K& key, const V& default_value) {
        std::unique_lock lock(mutex_);
        auto [it, inserted] = data_.try_emplace(key, default_value);
        return it->second;
    }

private:
    mutable std::shared_mutex mutex_;
    MapType data_;
};

//=============================================================================
// ThreadSafeValue - Thread-safe single value wrapper
//=============================================================================

template<typename T>
class ThreadSafeValue {
public:
    ThreadSafeValue() = default;
    explicit ThreadSafeValue(T value) : value_(std::move(value)) {}
    
    void Set(const T& value) {
        std::unique_lock lock(mutex_);
        value_ = value;
    }
    
    void Set(T&& value) {
        std::unique_lock lock(mutex_);
        value_ = std::move(value);
    }
    
    T Get() const {
        std::shared_lock lock(mutex_);
        return value_;
    }
    
    // Apply function and return result
    template<typename Fn>
    auto Apply(Fn&& fn) {
        std::unique_lock lock(mutex_);
        return fn(value_);
    }
    
    // Apply function (read-only)
    template<typename Fn>
    auto Apply(Fn&& fn) const {
        std::shared_lock lock(mutex_);
        return fn(value_);
    }

private:
    mutable std::shared_mutex mutex_;
    T value_{};
};

} // namespace experiments
