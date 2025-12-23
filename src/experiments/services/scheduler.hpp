#pragma once

/**
 * scheduler.hpp - Background Scheduler
 */

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

#include "cron.hpp"
#include "service_worker.hpp"

namespace experiments {

struct ScheduledJob {
    std::string id;
    std::string cron_expression;
    std::function<void()> callback;
    std::chrono::system_clock::time_point last_run;
    std::shared_ptr<CronExpression> cron;
    bool enabled{true};
};

class BackgroundScheduler {
public:
    BackgroundScheduler() {
        running_ = true;
        thread_ = std::thread([this]() { RunLoop(); });
    }
    
    ~BackgroundScheduler() {
        Stop();
    }
    
    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    
    bool ScheduleJob(const std::string& id, const std::string& cron_str, std::function<void()> cb) {
        std::lock_guard lock(mutex_);
        auto cron = std::make_shared<CronExpression>(cron_str);
        if (!cron->IsValid()) return false;
        
        ScheduledJob job;
        job.id = id;
        job.cron_expression = cron_str;
        job.callback = std::move(cb);
        job.cron = cron;
        job.last_run = std::chrono::system_clock::now();
        
        jobs_[id] = std::move(job);
        return true;
    }
    
    std::shared_ptr<ServiceWorker> RegisterWorker(const std::string& id, ServiceWorker::Config config) {
        std::lock_guard lock(mutex_);
        auto worker = std::make_shared<ServiceWorker>(id, std::move(config));
        workers_[id] = worker;
        if (worker->GetState() == ServiceWorker::State::Redundant) {
             worker->Start();
        }
        return worker;
    }
    
    std::shared_ptr<ServiceWorker> GetWorker(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = workers_.find(id);
        if (it != workers_.end()) return it->second;
        return nullptr;
    }
    
    size_t GetJobCount() const { return jobs_.size(); }
    size_t GetWorkerCount() const { return workers_.size(); }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ScheduledJob> jobs_;
    std::unordered_map<std::string, std::shared_ptr<ServiceWorker>> workers_;
    
    void RunLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running_) break;
            
            std::lock_guard lock(mutex_);
            auto now = std::chrono::system_clock::now();
            
            for (auto& [id, job] : jobs_) {
                if (!job.enabled) continue;
                
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - job.last_run).count();
                if (elapsed >= 60) {
                     if (job.cron->IsMatch(now)) {
                         job.callback();
                         job.last_run = now;
                     }
                }
            }
        }
    }
};

} // namespace experiments
