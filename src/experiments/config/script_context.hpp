#pragma once

/**
 * script_context.hpp - Script Context (shared data)
 */

#include <string>
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <memory>

namespace experiments {

// Script Priority
enum class ScriptPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

//=============================================================================
// Script Context (shared data)
//=============================================================================

class ScriptContext {
public:
    void Set(const std::string& key, const std::string& value) {
        std::lock_guard lock(mutex_);
        data_[key] = value;
    }
    
    std::optional<std::string> Get(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? std::optional{it->second} : std::nullopt;
    }
    
    void Remove(const std::string& key) {
        std::lock_guard lock(mutex_);
        data_.erase(key);
    }
    
    void Clear() {
        std::lock_guard lock(mutex_);
        data_.clear();
    }
    
    std::unordered_map<std::string, std::string> GetAll() const {
        std::shared_lock lock(mutex_);
        return data_;
    }
    
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

using ScriptContextPtr = std::shared_ptr<ScriptContext>;

} // namespace experiments
