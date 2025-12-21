/**
 * obs-node-runner.cpp - Script runner implementation
 * 
 * Implements script execution in the shared Node.js environment.
 */

#include "obs-node-runner.h"
#include "obs-node.h"

#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

namespace obs_node {

// External references to shared Node.js state
extern node::MultiIsolatePlatform* get_platform();
extern node::CommonEnvironmentSetup* get_setup();
extern bool is_initialized();

// ============================================================================
// ScriptRunner - executes scripts in the shared environment
// ============================================================================

ScriptRunner::ScriptRunner() = default;

ScriptRunner::~ScriptRunner() {
    terminate();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (timeout_thread_.joinable()) {
        timeout_thread_.join();
    }
}

bool ScriptRunner::run(const std::string& script_code, const Options& options) {
    if (running_) {
        return false;
    }

    if (!is_initialized()) {
        error_ = "Node.js not initialized";
        return false;
    }

    // Reset state
    terminate_requested_ = false;
    timed_out_ = false;
    succeeded_ = false;
    error_.clear();
    running_ = true;

    // Start worker thread
    worker_thread_ = std::thread([this, script_code, options] {
        thread_main(script_code, options);
    });

    // Start timeout thread if specified
    if (options.timeout.count() > 0) {
        auto& timeout = options.timeout;
        timeout_thread_ = std::thread([this, timeout] {
            timeout_thread(timeout);
        });
    }

    return true;
}

void ScriptRunner::terminate() {
    terminate_requested_ = true;
    
    if (isolate_) {
        isolate_->TerminateExecution();
    }
    
    cv_.notify_all();
}

bool ScriptRunner::is_running() const {
    return running_;
}

bool ScriptRunner::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    
    if (timeout.count() == 0) {
        cv_.wait(lock, [this]() { return !running_.load(); });
        return true;
    }
    
    return cv_.wait_for(lock, timeout, [this]() { return !running_.load(); });
}

void ScriptRunner::thread_main(const std::string& script_code, const Options& options) {
    node::MultiIsolatePlatform* platform = get_platform();
    node::CommonEnvironmentSetup* setup = get_setup();
    
    if (!platform || !setup) {
        error_ = "Node.js environment not available";
        succeeded_ = false;
        running_ = false;
        cv_.notify_all();
        if (options.on_complete) {
            options.on_complete(false, error_);
        }
        return;
    }

    isolate_ = setup->isolate();

    bool script_succeeded = false;
    std::string script_error;
    std::string script_result;

    {
        v8::Locker locker(isolate_);
        v8::Isolate::Scope isolate_scope(isolate_);
        v8::HandleScope handle_scope(isolate_);
        v8::Context::Scope context_scope(setup->context());

        v8::TryCatch try_catch(isolate_);

        if (terminate_requested_ || timed_out_) {
            script_error = timed_out_ ? "Script timed out" : "Script terminated";
            goto done;
        }

        // Compile and run
        {
            v8::Local<v8::String> source = 
                v8::String::NewFromUtf8(isolate_, script_code.c_str()).ToLocalChecked();
            
            v8::Local<v8::Script> script;
            if (!v8::Script::Compile(setup->context(), source).ToLocal(&script)) {
                if (try_catch.HasCaught()) {
                    v8::Local<v8::Value> exception = try_catch.Exception();
                    v8::String::Utf8Value utf8(isolate_, exception);
                    script_error = *utf8 ? *utf8 : "Compile error";
                } else {
                    script_error = "Failed to compile script";
                }
                goto done;
            }

            v8::Local<v8::Value> result;
            if (!script->Run(setup->context()).ToLocal(&result)) {
                if (try_catch.HasCaught()) {
                    v8::Local<v8::Value> exception = try_catch.Exception();
                    v8::String::Utf8Value utf8(isolate_, exception);
                    script_error = *utf8 ? *utf8 : "Runtime error";
                } else if (timed_out_) {
                    script_error = "Script timed out";
                } else if (terminate_requested_) {
                    script_error = "Script terminated";
                } else {
                    script_error = "Script execution failed";
                }
                goto done;
            }

            // Capture result as string
            if (!result.IsEmpty() && !result->IsUndefined()) {
                v8::String::Utf8Value utf8(isolate_, result);
                if (*utf8) {
                    script_result = *utf8;
                }
            }
        }

        // Run event loop if requested
        if (options.spin_event_loop && !terminate_requested_ && !timed_out_) {
            uv_loop_t* loop = setup->event_loop();
            bool more;
            do {
                if (terminate_requested_ || timed_out_) break;
                more = uv_run(loop, UV_RUN_NOWAIT);
                platform->DrainTasks(isolate_);
            } while (more && !terminate_requested_ && !timed_out_);
        }

        script_succeeded = !terminate_requested_ && !timed_out_;
        if (timed_out_) {
            script_error = "Script timed out";
        }

done:
        ;
    }

    isolate_ = nullptr;
    succeeded_ = script_succeeded;
    error_ = script_error;
    result_ = script_result;
    running_ = false;
    cv_.notify_all();

    if (options.on_complete) {
        options.on_complete(succeeded_, error_);
    }
}

void ScriptRunner::timeout_thread(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    
    bool completed = cv_.wait_for(lock, timeout, [this]() {
        return !running_.load() || terminate_requested_.load();
    });

    if (!completed && running_ && !terminate_requested_) {
        timed_out_ = true;
        if (isolate_) {
            isolate_->TerminateExecution();
        }
    }
}

} // namespace obs_node

// ============================================================================
// C API Implementation
// ============================================================================

struct obs_node_script {
    std::unique_ptr<obs_node::ScriptRunner> runner;
    obs_node_script_callback callback;
    void* user_data;
};

obs_node_script_t obs_node_run_script(
    const char* script_code,
    const obs_node_script_options_t* options
) {
    if (!script_code) return nullptr;

    auto* script = new obs_node_script();
    script->runner = std::make_unique<obs_node::ScriptRunner>();
    script->callback = options ? options->on_complete : nullptr;
    script->user_data = options ? options->user_data : nullptr;

    obs_node::ScriptRunner::Options runner_options;
    
    if (options) {
        runner_options.timeout = std::chrono::milliseconds(options->timeout_ms);
        runner_options.spin_event_loop = options->spin_event_loop;
        
        if (options->on_complete) {
            runner_options.on_complete = [script](bool success, const std::string& error) {
                if (script->callback) {
                    script->callback(script, success, error.c_str(), script->user_data);
                }
            };
        }
    }

    if (!script->runner->run(script_code, runner_options)) {
        delete script;
        return nullptr;
    }

    return script;
}

void obs_node_terminate_script(obs_node_script_t script) {
    if (script && script->runner) {
        script->runner->terminate();
    }
}

bool obs_node_script_is_running(obs_node_script_t script) {
    return script && script->runner && script->runner->is_running();
}

bool obs_node_script_wait(obs_node_script_t script, uint32_t timeout_ms) {
    if (!script || !script->runner) return true;
    return script->runner->wait(std::chrono::milliseconds(timeout_ms));
}

void obs_node_script_free(obs_node_script_t script) {
    if (script) {
        if (script->runner && script->runner->is_running()) {
            script->runner->terminate();
            script->runner->wait();
        }
        delete script;
    }
}
