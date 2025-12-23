#pragma once

/**
 * metrics.hpp - Memory and Execution Metrics
 */

#include <chrono>
#include <cstddef>

namespace experiments {

//=============================================================================
// Metrics
//=============================================================================

struct MemoryMetrics {
    size_t heap_size_limit{0};
    size_t total_heap_size{0};
    size_t used_heap_size{0};
    size_t external_memory{0};
    
    double HeapUsagePercent() const {
        return heap_size_limit > 0 ? (100.0 * used_heap_size / heap_size_limit) : 0;
    }
};

struct ExecutionMetrics {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::chrono::milliseconds duration{0};
    size_t scripts_executed{0};
    size_t scripts_failed{0};
    size_t scripts_timed_out{0};
};

struct EnvironmentMetrics {
    MemoryMetrics memory;
    ExecutionMetrics execution;
    size_t queue_size{0};
    bool is_running{false};
};

} // namespace experiments
