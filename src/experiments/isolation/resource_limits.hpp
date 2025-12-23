#pragma once

/**
 * resource_limits.hpp - Resource Limits for Script Execution
 */

#include <chrono>
#include <cstddef>

namespace experiments {

struct ResourceLimits {
    // Memory limits
    size_t max_heap_size_mb{512};
    size_t max_old_space_mb{384};
    size_t max_young_space_mb{64};
    size_t max_array_buffer_mb{512};
    
    // Execution limits
    std::chrono::milliseconds cpu_time_limit{0};
    std::chrono::milliseconds wall_time_limit{0};
    size_t max_stack_depth{1024};
    size_t max_async_stack_depth{32};
    
    // Data structure limits
    size_t max_string_length{256 * 1024 * 1024};
    size_t max_array_length{4294967295};
    size_t max_object_properties{16 * 1024 * 1024};
    
    // I/O limits
    size_t max_open_files{100};
    size_t max_file_size_bytes{100 * 1024 * 1024};
    size_t max_network_connections{50};
    size_t max_http_response_bytes{50 * 1024 * 1024};
    
    // Rate limits
    size_t max_requests_per_second{100};
    size_t max_file_ops_per_second{1000};
    
    // Presets
    static ResourceLimits Minimal() {
        ResourceLimits limits;
        limits.max_heap_size_mb = 64;
        limits.max_old_space_mb = 48;
        limits.cpu_time_limit = std::chrono::seconds(5);
        limits.max_open_files = 10;
        limits.max_network_connections = 5;
        return limits;
    }
    
    static ResourceLimits Standard() {
        return ResourceLimits{};
    }
    
    static ResourceLimits Generous() {
        ResourceLimits limits;
        limits.max_heap_size_mb = 2048;
        limits.max_old_space_mb = 1536;
        limits.max_array_buffer_mb = 2048;
        limits.max_open_files = 500;
        limits.max_network_connections = 200;
        return limits;
    }
    
    static ResourceLimits Unlimited() {
        ResourceLimits limits;
        limits.cpu_time_limit = std::chrono::milliseconds(0);
        limits.wall_time_limit = std::chrono::milliseconds(0);
        limits.max_heap_size_mb = 0;
        return limits;
    }
};

} // namespace experiments
