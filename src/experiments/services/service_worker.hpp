#pragma once

/**
 * service_worker.hpp - Background Service Worker
 */

#include <string>
#include <mutex>
#include <atomic>

namespace experiments {

class ServiceWorker {
public:
    enum class State {
        Installing,
        Installed,
        Activating,
        Active,
        Redundant,
        Error
    };
    
    struct Config {
        std::string script_path;
        std::string scope;
        bool auto_start{true};
    };
    
    ServiceWorker(std::string id, Config config) 
        : id_(std::move(id)), config_(std::move(config)) {}
        
    void Start() {
        std::lock_guard lock(mutex_);
        if (state_ == State::Active) return;
        
        SetState(State::Installing);
        SetState(State::Installed);
        SetState(State::Activating);
        SetState(State::Active);
    }
    
    void Stop() {
        std::lock_guard lock(mutex_);
        SetState(State::Redundant);
    }
    
    void DispatchEvent(const std::string& event_name, const std::string& /*payload*/) {
        std::lock_guard lock(mutex_);
        if (state_ != State::Active) return;
        
        last_event_ = event_name;
        event_count_++;
    }
    
    State GetState() const { return state_; }
    std::string GetStateName() const {
        switch (state_) {
            case State::Installing: return "installing";
            case State::Installed: return "installed";
            case State::Activating: return "activating";
            case State::Active: return "active";
            case State::Redundant: return "redundant";
            case State::Error: return "error";
        }
        return "unknown";
    }
    
    const std::string& GetId() const { return id_; }
    size_t GetEventCount() const { return event_count_; }
    
private:
    std::string id_;
    Config config_;
    std::atomic<State> state_{State::Redundant};
    std::string last_event_;
    size_t event_count_{0};
    mutable std::mutex mutex_;
    
    void SetState(State s) {
        state_ = s;
    }
};

} // namespace experiments
