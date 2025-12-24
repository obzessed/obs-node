#pragma once

/**
 * reactive.hpp - Reactive Expression System
 * 
 * Provides reactive values and expressions that automatically
 * re-evaluate when their dependencies change.
 * 
 * Usage:
 *   ReactiveContext ctx;
 *   auto x = ctx.CreateValue("x", 10);
 *   auto y = ctx.CreateValue("y", 20);
 *   
 *   auto sum = ctx.CreateExpression("x + y");
 *   sum->OnChange([](const ExpressionValue& val) {
 *       std::cout << "Sum changed to: " << val.AsNumber() << std::endl;
 *   });
 *   
 *   x->Set(15);  // Triggers re-evaluation, outputs "Sum changed to: 35"
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>

#include "types.hpp"

namespace experiments {

// Forward declarations
class ReactiveValue;
class ReactiveExpression;
class ReactiveContext;

using ReactiveValuePtr = std::shared_ptr<ReactiveValue>;
using ReactiveExpressionPtr = std::shared_ptr<ReactiveExpression>;

//=============================================================================
// ReactiveValue - An observable value that notifies dependents on change
//=============================================================================

class ReactiveValue : public std::enable_shared_from_this<ReactiveValue> {
public:
    using ChangeCallback = std::function<void(const ExpressionValue&)>;
    
    ReactiveValue(std::string name, ExpressionValue initial = ExpressionValue())
        : name_(std::move(name)), value_(std::move(initial)), version_(0) {}
    
    // Get the current value
    ExpressionValue Get() const {
        std::shared_lock lock(mutex_);
        return value_;
    }
    
    // Set the value (triggers notifications if changed)
    void Set(const ExpressionValue& newValue) {
        bool changed = false;
        {
            std::unique_lock lock(mutex_);
            if (value_ != newValue) {
                value_ = newValue;
                ++version_;
                changed = true;
            }
        }
        
        if (changed) {
            NotifyDependents();
        }
    }
    
    // Subscribe to changes
    size_t Subscribe(ChangeCallback callback) {
        std::unique_lock lock(mutex_);
        size_t id = next_subscriber_id_++;
        subscribers_[id] = std::move(callback);
        return id;
    }
    
    // Unsubscribe
    void Unsubscribe(size_t id) {
        std::unique_lock lock(mutex_);
        subscribers_.erase(id);
    }
    
    // Register a dependent expression
    void AddDependent(ReactiveExpression* expr) {
        std::unique_lock lock(mutex_);
        dependents_.insert(expr);
    }
    
    void RemoveDependent(ReactiveExpression* expr) {
        std::unique_lock lock(mutex_);
        dependents_.erase(expr);
    }
    
    const std::string& GetName() const { return name_; }
    uint64_t GetVersion() const { return version_.load(); }

private:
    void NotifyDependents();  // Defined after ReactiveExpression
    
    std::string name_;
    ExpressionValue value_;
    std::atomic<uint64_t> version_;
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<size_t, ChangeCallback> subscribers_;
    std::unordered_set<ReactiveExpression*> dependents_;
    size_t next_subscriber_id_{0};
};

//=============================================================================
// ReactiveExpression - Expression that re-evaluates on dependency change
//=============================================================================

class ReactiveExpression : public std::enable_shared_from_this<ReactiveExpression> {
public:
    using ChangeCallback = std::function<void(const ExpressionValue&)>;
    
    ReactiveExpression(std::string expression, ReactiveContext* context);
    ~ReactiveExpression();
    
    // Get the current computed value
    ExpressionValue Get() const {
        std::shared_lock lock(mutex_);
        return cached_value_;
    }
    
    // Force re-evaluation
    void Invalidate() {
        Evaluate();
        NotifySubscribers();
    }
    
    // Subscribe to value changes
    size_t OnChange(ChangeCallback callback) {
        std::unique_lock lock(mutex_);
        size_t id = next_subscriber_id_++;
        subscribers_[id] = std::move(callback);
        return id;
    }
    
    void OffChange(size_t id) {
        std::unique_lock lock(mutex_);
        subscribers_.erase(id);
    }
    
    // Check if expression is valid
    bool IsValid() const { return is_valid_; }
    std::string GetError() const { return error_; }
    
    // Get tracked dependencies
    std::vector<std::string> GetDependencies() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> deps;
        for (const auto& [name, _] : tracked_dependencies_) {
            deps.push_back(name);
        }
        return deps;
    }

private:
    void Evaluate();
    void NotifySubscribers();
    void TrackDependency(const std::string& name, ReactiveValuePtr value);
    void ClearDependencies();
    
    std::string expression_;
    ReactiveContext* context_;
    ExpressionValue cached_value_;
    bool is_valid_{false};
    std::string error_;
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<size_t, ChangeCallback> subscribers_;
    std::unordered_map<std::string, ReactiveValuePtr> tracked_dependencies_;
    size_t next_subscriber_id_{0};
    
    friend class ReactiveContext;
};

//=============================================================================
// ReactiveContext - Manages reactive values and expressions
//=============================================================================

class ReactiveContext {
public:
    ReactiveContext() = default;
    ~ReactiveContext() {
        // Clean up all expressions first
        expressions_.clear();
        values_.clear();
    }
    
    // Create a reactive value
    ReactiveValuePtr CreateValue(const std::string& name, const ExpressionValue& initial = ExpressionValue()) {
        std::unique_lock lock(mutex_);
        auto value = std::make_shared<ReactiveValue>(name, initial);
        values_[name] = value;
        return value;
    }
    
    // Get an existing value
    ReactiveValuePtr GetValue(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = values_.find(name);
        return it != values_.end() ? it->second : nullptr;
    }
    
    // Set a value (creates if doesn't exist)
    void Set(const std::string& name, const ExpressionValue& value) {
        ReactiveValuePtr rv;
        {
            std::unique_lock lock(mutex_);
            auto it = values_.find(name);
            if (it == values_.end()) {
                rv = std::make_shared<ReactiveValue>(name, value);
                values_[name] = rv;
                return;
            }
            rv = it->second;
        }
        rv->Set(value);
    }
    
    // Get a value
    ExpressionValue Get(const std::string& name) const {
        auto rv = GetValue(name);
        return rv ? rv->Get() : ExpressionValue();
    }
    
    // Create a reactive expression
    ReactiveExpressionPtr CreateExpression(const std::string& expr) {
        auto reactive = std::make_shared<ReactiveExpression>(expr, this);
        {
            std::unique_lock lock(mutex_);
            expressions_.push_back(reactive);
        }
        reactive->Invalidate();  // Initial evaluation
        return reactive;
    }
    
    // Batch updates - defer notifications until complete
    template<typename Fn>
    void Batch(Fn&& updates) {
        batching_ = true;
        updates();
        batching_ = false;
        
        // Process pending invalidations
        for (auto* expr : pending_invalidations_) {
            expr->Invalidate();
        }
        pending_invalidations_.clear();
    }
    
    void ScheduleInvalidation(ReactiveExpression* expr) {
        if (batching_) {
            pending_invalidations_.insert(expr);
        } else {
            expr->Invalidate();
        }
    }
    
    // Get all value names
    std::vector<std::string> GetValueNames() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : values_) {
            names.push_back(name);
        }
        return names;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ReactiveValuePtr> values_;
    std::vector<ReactiveExpressionPtr> expressions_;
    
    std::atomic<bool> batching_{false};
    std::unordered_set<ReactiveExpression*> pending_invalidations_;
    
    friend class ReactiveExpression;
};

//=============================================================================
// Implementation Details
//=============================================================================

inline void ReactiveValue::NotifyDependents() {
    std::vector<ReactiveExpression*> deps;
    std::vector<ChangeCallback> callbacks;
    
    {
        std::shared_lock lock(mutex_);
        deps.assign(dependents_.begin(), dependents_.end());
        for (const auto& [_, cb] : subscribers_) {
            callbacks.push_back(cb);
        }
    }
    
    // Notify direct subscribers
    ExpressionValue val = Get();
    for (const auto& cb : callbacks) {
        cb(val);
    }
    
    // Invalidate dependent expressions
    for (auto* expr : deps) {
        expr->Invalidate();
    }
}

inline ReactiveExpression::ReactiveExpression(std::string expression, ReactiveContext* context)
    : expression_(std::move(expression)), context_(context) {}

inline ReactiveExpression::~ReactiveExpression() {
    ClearDependencies();
}

inline void ReactiveExpression::Evaluate() {
    ClearDependencies();
    
    // Create expression context from reactive values
    ExpressionContext evalCtx;
    
    // Track which values are accessed during evaluation
    // We'll use a simple approach: pre-populate with all values
    // A more sophisticated approach would intercept variable access
    
    for (const auto& name : context_->GetValueNames()) {
        auto rv = context_->GetValue(name);
        if (rv) {
            evalCtx.Set(name, rv->Get());
            TrackDependency(name, rv);
        }
    }
    
    // Evaluate the expression
    ExpressionEngine engine;
    auto result = engine.Evaluate(expression_, evalCtx);
    
    {
        std::unique_lock lock(mutex_);
        if (result.success) {
            cached_value_ = result.value;
            is_valid_ = true;
            error_.clear();
        } else {
            is_valid_ = false;
            error_ = result.error;
        }
    }
}

inline void ReactiveExpression::NotifySubscribers() {
    std::vector<ChangeCallback> callbacks;
    ExpressionValue val;
    
    {
        std::shared_lock lock(mutex_);
        val = cached_value_;
        for (const auto& [_, cb] : subscribers_) {
            callbacks.push_back(cb);
        }
    }
    
    for (const auto& cb : callbacks) {
        cb(val);
    }
}

inline void ReactiveExpression::TrackDependency(const std::string& name, ReactiveValuePtr value) {
    std::unique_lock lock(mutex_);
    if (tracked_dependencies_.find(name) == tracked_dependencies_.end()) {
        tracked_dependencies_[name] = value;
        value->AddDependent(this);
    }
}

inline void ReactiveExpression::ClearDependencies() {
    std::unique_lock lock(mutex_);
    for (auto& [_, value] : tracked_dependencies_) {
        value->RemoveDependent(this);
    }
    tracked_dependencies_.clear();
}

} // namespace experiments
