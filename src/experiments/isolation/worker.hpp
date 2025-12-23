#pragma once

/**
 * worker.hpp - Worker Thread Isolation
 */

#include <string>
#include <optional>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

#include "resource_limits.hpp"
#include "sandbox.hpp"
#include "../core/logger.hpp"

namespace experiments {

//=============================================================================
// Worker Message - For inter-thread communication
//=============================================================================

struct WorkerMessage {
    enum class Type {
        Data,       // Regular data message
        Error,      // Error from worker
        Terminate,  // Terminate signal
        Ready       // Worker ready signal
    };

    Type type{Type::Data};
    std::string data;      // JSON-serialized data
    std::string error;
    std::chrono::steady_clock::time_point sent_at;

    WorkerMessage() : sent_at(std::chrono::steady_clock::now()) {}
    WorkerMessage(Type t, std::string d = "")
        : type(t), data(std::move(d)), sent_at(std::chrono::steady_clock::now()) {}

    static WorkerMessage Data(const std::string& payload) {
        return WorkerMessage(Type::Data, payload);
    }

    static WorkerMessage Error(const std::string& err) {
        WorkerMessage msg(Type::Error);
        msg.error = err;
        return msg;
    }

    static WorkerMessage Terminate() {
        return WorkerMessage(Type::Terminate);
    }
};

//=============================================================================
// Message Port - Bidirectional communication channel
//=============================================================================

class MessagePort {
public:
    MessagePort() = default;

    // Send a message
    void PostMessage(const std::string& data) {
        std::lock_guard lock(mutex_);
        queue_.push(WorkerMessage::Data(data));
        cv_.notify_one();
    }

    void PostMessage(WorkerMessage msg) {
        std::lock_guard lock(mutex_);
        queue_.push(std::move(msg));
        cv_.notify_one();
    }

    // Receive a message (blocking)
    WorkerMessage Receive(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        std::unique_lock lock(mutex_);

        if (timeout.count() > 0) {
            cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || closed_; });
        } else {
            cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
        }

        if (queue_.empty()) {
            return WorkerMessage(WorkerMessage::Type::Terminate);
        }

        auto msg = std::move(queue_.front());
        queue_.pop();
        return msg;
    }

    // Try to receive (non-blocking)
    std::optional<WorkerMessage> TryReceive() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return std::nullopt;

        auto msg = std::move(queue_.front());
        queue_.pop();
        return msg;
    }

    // Check if there are pending messages
    bool HasMessages() const {
        std::lock_guard lock(mutex_);
        return !queue_.empty();
    }

    // Close the port
    void Close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    bool IsClosed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<WorkerMessage> queue_;
    bool closed_{false};
};

using MessagePortPtr = std::shared_ptr<MessagePort>;

//=============================================================================
// Worker Options
//=============================================================================

struct WorkerOptions {
    std::string name{"Worker"};
    ResourceLimits resource_limits;
    SandboxConfig sandbox_config;
    bool terminate_on_error{false};
    std::chrono::milliseconds startup_timeout{5000};
    std::vector<std::string> env_vars;  // Environment variables to expose
};

//=============================================================================
// Worker - Isolated script execution in separate thread
//=============================================================================

class Worker {
public:
    enum class State {
        Created,
        Starting,
        Running,
        Terminated,
        Error
    };

    Worker(const std::string& script, WorkerOptions options = {})
        : script_(script)
        , options_(std::move(options))
        , state_(State::Created)
        , parent_port_(std::make_shared<MessagePort>())
        , worker_port_(std::make_shared<MessagePort>()) {}

    ~Worker() {
        Terminate();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Start the worker
    bool Start() {
        if (state_ != State::Created) return false;

        state_ = State::Starting;
        thread_ = std::thread([this]() { Run(); });

        // Wait for ready signal
        auto msg = parent_port_->Receive(options_.startup_timeout);
        if (msg.type == WorkerMessage::Type::Ready) {
            state_ = State::Running;
            return true;
        }

        state_ = State::Error;
        error_ = msg.error.empty() ? "Startup timeout" : msg.error;
        return false;
    }

    // Post message to worker
    void PostMessage(const std::string& data) {
        if (state_ == State::Running) {
            worker_port_->PostMessage(data);
        }
    }

    // Receive message from worker
    std::optional<WorkerMessage> Receive(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(0)
    ) {
        if (timeout.count() > 0) {
            return parent_port_->Receive(timeout);
        }
        return parent_port_->TryReceive();
    }

    // Set message handler
    using MessageHandler = std::function<void(const WorkerMessage&)>;
    void OnMessage(MessageHandler handler) {
        message_handler_ = std::move(handler);
    }

    // Terminate the worker
    void Terminate() {
        if (state_ == State::Running || state_ == State::Starting) {
            worker_port_->PostMessage(WorkerMessage::Terminate());
            worker_port_->Close();
            state_ = State::Terminated;
        }
    }

    // Getters
    State GetState() const { return state_; }
    std::string GetError() const { return error_; }
    std::string GetName() const { return options_.name; }

    // Get port for sending to worker
    MessagePortPtr GetPort() { return worker_port_; }

private:
    std::string script_;
    WorkerOptions options_;
    std::atomic<State> state_;
    std::string error_;
    std::thread thread_;
    MessagePortPtr parent_port_;  // For parent to receive from worker
    MessagePortPtr worker_port_;  // For worker to receive from parent
    MessageHandler message_handler_;

    void Run() {
        try {
            // In real implementation, would create isolated V8 context here
            LOG_INFO("Worker", "Worker " + options_.name + " started");

            // Signal ready
            parent_port_->PostMessage(WorkerMessage(WorkerMessage::Type::Ready));

            // Message loop
            while (state_ == State::Running) {
                auto msg = worker_port_->Receive(std::chrono::milliseconds(100));

                if (msg.type == WorkerMessage::Type::Terminate) {
                    break;
                }

                if (msg.type == WorkerMessage::Type::Data) {
                    // Process message (in real impl, would execute in V8)
                    // Echo back for now
                    parent_port_->PostMessage("Received: " + msg.data);
                }
            }
            
            LOG_INFO("Worker", "Worker " + options_.name + " stopped");
        } catch (const std::exception& e) {
            error_ = e.what();
            state_ = State::Error;
            parent_port_->PostMessage(WorkerMessage::Error(error_));
        }
    }
};

using WorkerPtr = std::shared_ptr<Worker>;

} // namespace experiments
