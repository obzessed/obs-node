/**
 * obs-node.cpp - Node.js embedding for OBS plugin
 * 
 * This file implements the Node.js runtime embedding using the
 * official C++ embedder API (libnode).
 */

#include "obs-node.h"

// Node.js headers
#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

#include <memory>
#include <string>
#include <vector>

#define ISOLATE_THREAD_POOL_SIZE 4

// Node.js runtime state
static std::unique_ptr<node::MultiIsolatePlatform> g_platform;
static std::unique_ptr<node::CommonEnvironmentSetup> g_setup;
static std::shared_ptr<node::InitializationResult> g_init_result;
static bool g_initialized = false;

// Accessor functions for ScriptRunner
namespace obs_node {
    node::MultiIsolatePlatform* get_platform() {
        return g_platform.get();
    }
    
    std::shared_ptr<node::InitializationResult> get_init_result() {
        return g_init_result;
    }
    
    node::CommonEnvironmentSetup* get_setup() {
        return g_setup.get();
    }
    
    bool is_initialized() {
        return g_initialized;
    }
}

// Stub argc/argv for embedded usage
static int g_argc = 1;
static char g_arg0[] = "obs-plugin-node";
static char* g_argv[] = { g_arg0, nullptr };

extern "C" void obs_node_load(void)
{
    if (g_initialized) {
        obs_log(LOG_WARNING, "obs_node_load called but already initialized");
        return;
    }

    obs_log(LOG_INFO, "Initializing Node.js v%s runtime...", NODE_VERSION_STRING);

    try {
        char** argv = uv_setup_args(g_argc, g_argv);

        // Prepare args for Node.js
        std::vector<std::string> args(argv, argv + g_argc);

        // Initialize Node.js per-process state
        // We handle V8 initialization ourselves for better control
        g_init_result = node::InitializeOncePerProcess(args, {
            node::ProcessInitializationFlags::kNoInitializeV8,
            node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
            node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput
        });

        if (g_init_result->early_return() != 0) {
            for (const std::string& error : g_init_result->errors()) {
                obs_log(LOG_ERROR, "Node.js init error: %s", error.c_str());
            }
            obs_log(LOG_ERROR, "Failed to initialize Node.js per-process state");
            return;
        }

        // Create V8 platform
        g_platform = node::MultiIsolatePlatform::Create(ISOLATE_THREAD_POOL_SIZE);
        v8::V8::InitializePlatform(g_platform.get());
        v8::V8::Initialize();

        // Create Node.js environment
        std::vector<std::string> errors;
        g_setup = node::CommonEnvironmentSetup::Create(
            g_platform.get(),
            &errors,
            g_init_result->args(),
            g_init_result->exec_args()
        );

        if (!g_setup) {
            for (const std::string& err : errors) {
                obs_log(LOG_ERROR, "Node.js setup error: %s", err.c_str());
            }
            obs_log(LOG_ERROR, "Failed to create Node.js environment");
            v8::V8::Dispose();
            v8::V8::DisposePlatform();
            node::TearDownOncePerProcess();
            return;
        }

        // Generate bootstrap script with OBS version info
        const char* obs_ver = obs_get_version_string();
        uint32_t ver_int = obs_get_version();
        int major = (ver_int >> 24) & 0xFF;
        int minor = (ver_int >> 16) & 0xFF;
        int patch = ver_int & 0xFFFF;
        
        // Check capabilities at compile time and runtime
        bool has_frontend = false;
        bool has_scripting = false;
        bool has_browser = false;
        
#ifdef ENABLE_FRONTEND_API
        has_frontend = true;
#endif
        // Check for scripting module
        has_scripting = obs_get_module("obs-scripting") != nullptr;
        // Check for browser source
        has_browser = obs_get_module("obs-browser") != nullptr;
        
        // Bootstrap script (plain JavaScript, run via v8::Script after LoadEnvironment)
        std::string bootstrap = 
            "globalThis.obs = {\n"
            "    version: {\n"
            "        string: '" + std::string(obs_ver ? obs_ver : "unknown") + "',\n"
            "        major: " + std::to_string(major) + ",\n"
            "        minor: " + std::to_string(minor) + ",\n"
            "        patch: " + std::to_string(patch) + "\n"
            "    },\n"
            "    capabilities: {\n"
            "        frontend: " + std::string(has_frontend ? "true" : "false") + ",\n"
            "        scripting: " + std::string(has_scripting ? "true" : "false") + ",\n"
            "        browser: " + std::string(has_browser ? "true" : "false") + ",\n"
#ifdef _WIN32
            "        platform: 'windows'\n"
#elif defined(__APPLE__)
            "        platform: 'macos'\n"
#else
            "        platform: 'linux'\n"
#endif
            "    }\n"
            "};\n";

        // Initialize environment and run bootstrap
        {
            v8::Isolate* isolate = g_setup->isolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolate_scope(isolate);
            v8::HandleScope handle_scope(isolate);
            v8::Context::Scope context_scope(g_setup->context());

            // First, call LoadEnvironment with empty script for basic Node.js setup
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(
                g_setup->env(),
                "// Node.js initialized\n"
            );

            if (result.IsEmpty()) {
                obs_log(LOG_ERROR, "Failed to load Node.js environment");
                g_setup.reset();
                v8::V8::Dispose();
                v8::V8::DisposePlatform();
                node::TearDownOncePerProcess();
                return;
            }

            // Now run our bootstrap using v8::Script
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::String> source = 
                v8::String::NewFromUtf8(isolate, bootstrap.c_str()).ToLocalChecked();
            
            v8::Local<v8::Script> script;
            if (!v8::Script::Compile(g_setup->context(), source).ToLocal(&script)) {
                obs_log(LOG_ERROR, "Failed to compile bootstrap script");
                if (try_catch.HasCaught()) {
                    v8::String::Utf8Value err(isolate, try_catch.Exception());
                    obs_log(LOG_ERROR, "  Error: %s", *err);
                }
            } else {
                script->Run(g_setup->context());
                obs_log(LOG_INFO, "Bootstrap script executed");
            }
        }

        g_initialized = true;
        obs_log(LOG_INFO, "Node.js runtime initialized successfully");

    } catch (const std::exception& e) {
        obs_log(LOG_ERROR, "Exception during Node.js init: %s", e.what());
    } catch (...) {
        obs_log(LOG_ERROR, "Unknown exception during Node.js initialization");
    }
}

extern "C" void obs_node_unload(void)
{
    if (!g_initialized) {
        return;
    }

    obs_log(LOG_INFO, "Shutting down Node.js runtime...");

    try {
        // Cleanup Node.js environment
        if (g_setup) {
            if (node::Environment* env = g_setup->env()) {
                node::Stop(env);
            }
            g_setup.reset();
        }

        // Cleanup V8 and platform
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
        g_platform.reset();

        // Clear init result
        g_init_result.reset();

        // Teardown per-process state
        node::TearDownOncePerProcess();

        g_initialized = false;
        obs_log(LOG_INFO, "Node.js runtime shutdown complete");

    } catch (const std::exception& e) {
        obs_log(LOG_ERROR, "Exception during Node.js shutdown: %s", e.what());
    } catch (...) {
        obs_log(LOG_ERROR, "Unknown exception during Node.js shutdown");
    }
}