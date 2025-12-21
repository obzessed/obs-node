/**
 * obs-node-runner.h - Script runner for Node.js embedding
 * 
 * Provides multi-isolate script execution with timeout support.
 * Each script runs in its own isolate on a separate thread.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <node/v8.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to a running script
typedef struct obs_node_script* obs_node_script_t;

// Script completion callback
typedef void (*obs_node_script_callback)(
    obs_node_script_t script,
    bool success,
    const char* error_message,
    void* user_data
);

// Script execution options
typedef struct obs_node_script_options {
    uint32_t timeout_ms;        // Timeout in milliseconds (0 = no timeout)
    bool spin_event_loop;       // Whether to spin the event loop after loading
    void* user_data;            // User data passed to callback
    obs_node_script_callback on_complete;  // Completion callback
} obs_node_script_options_t;

/**
 * Run a JavaScript script in a new isolate on a separate thread.
 * 
 * @param script_code The JavaScript code to execute
 * @param options Script execution options (can be NULL for defaults)
 * @return Script handle, or NULL on error
 */
obs_node_script_t obs_node_run_script(
    const char* script_code,
    const obs_node_script_options_t* options
);

/**
 * Terminate a running script.
 * This will interrupt the script and clean up resources.
 * 
 * @param script The script handle to terminate
 */
void obs_node_terminate_script(obs_node_script_t script);

/**
 * Check if a script is still running.
 * 
 * @param script The script handle
 * @return true if running, false otherwise
 */
bool obs_node_script_is_running(obs_node_script_t script);

/**
 * Wait for a script to complete.
 * 
 * @param script The script handle
 * @param timeout_ms Maximum time to wait (0 = wait forever)
 * @return true if script completed, false if timeout
 */
bool obs_node_script_wait(obs_node_script_t script, uint32_t timeout_ms);

/**
 * Free a script handle.
 * If the script is still running, it will be terminated first.
 * 
 * @param script The script handle to free
 */
void obs_node_script_free(obs_node_script_t script);

#ifdef __cplusplus
}
#endif

// C++ API
#ifdef __cplusplus

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace obs_node {

class ScriptRunner {
public:
    struct Options {
        std::chrono::milliseconds timeout{0};  // 0 = no timeout
        bool spin_event_loop = true;
        std::function<void(bool success, const std::string& error)> on_complete;
    };

    ScriptRunner();
    ~ScriptRunner();

    // Run script in new isolate on separate thread
    bool run(const std::string& script_code, const Options& options = {});

    // Terminate the running script
    void terminate();

    // Check if running
    bool is_running() const;

    // Wait for completion
    bool wait(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    // Get error message if failed
    const std::string& get_error() const { return error_; }

    // Get script return value (as string)
    const std::string& get_result() const { return result_; }

    // Get result (success/failure)
    bool succeeded() const { return succeeded_; }

private:
    void thread_main(const std::string& script_code, const Options& options);
    void timeout_thread(std::chrono::milliseconds timeout);

    std::thread worker_thread_;
    std::thread timeout_thread_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> terminate_requested_{false};
    std::atomic<bool> timed_out_{false};
    
    std::mutex mutex_;
    std::condition_variable cv_;
    
    std::string error_;
    std::string result_;
    bool succeeded_ = false;
    
    // V8 isolate for termination
    v8::Isolate* isolate_ = nullptr;
};

} // namespace obs_node

#endif // __cplusplus
