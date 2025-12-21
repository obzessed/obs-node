/**
 * obs-node.cpp - Node.js embedding for OBS plugin
 * 
 * This file implements the Node.js runtime embedding using the
 * official C++ embedder API (libnode).
 */

#include "obs-node.h"
#include "./bindings/obs-bindings.h"

// Node.js headers
#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

#include <memory>
#include <string>
#include <vector>
#include <mutex>

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

    // Console callback management
    static std::mutex g_console_mutex;
    static ConsoleCallback g_console_callback = nullptr;

    void SetConsoleCallback(ConsoleCallback cb) {
        std::lock_guard<std::mutex> lock(g_console_mutex);
        g_console_callback = cb;
    }

    // Internal binding exposed to JS to route console messages to C++
    static void InternalConsoleLog(const v8::FunctionCallbackInfo<v8::Value>& args) {
        v8::Isolate* isolate = args.GetIsolate();
        if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) return;

        v8::String::Utf8Value type(isolate, args[0]);
        v8::String::Utf8Value msg(isolate, args[1]);

        std::lock_guard<std::mutex> lock(g_console_mutex);
        if (g_console_callback) {
            std::string typeStr = *type ? *type : "log";
            std::string msgStr = *msg ? *msg : "";
            g_console_callback(typeStr, msgStr);
        }
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

            // First, call LoadEnvironment with CommonJS entry that provides require
            // Using the internal embedder hook to get access to require
            v8::MaybeLocal<v8::Value> result = node::LoadEnvironment(
                g_setup->env(),
                "const { createRequire } = require('module');\n"
                "globalThis.require = createRequire(process.cwd() + '/obs-script.js');\n"
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

            // Initialize OBS bindings
            obs_bindings::Initialize(isolate, g_setup->context());
            obs_log(LOG_INFO, "OBS JavaScript bindings initialized");

            // Register obs.internal.log for console redirection
            v8::Local<v8::Object> obs = v8::Local<v8::Object>::Cast(
                g_setup->context()->Global()->Get(g_setup->context(), 
                    v8::String::NewFromUtf8(isolate, "obs").ToLocalChecked()).ToLocalChecked());
            
            v8::Local<v8::Object> internal = v8::Object::New(isolate);
            internal->Set(g_setup->context(),
                v8::String::NewFromUtf8(isolate, "log").ToLocalChecked(),
                v8::Function::New(g_setup->context(), obs_node::InternalConsoleLog).ToLocalChecked()
            ).Check();

            obs->Set(g_setup->context(),
                v8::String::NewFromUtf8(isolate, "internal").ToLocalChecked(),
                internal
            ).Check();

            // Register obs: module scheme for require('obs:sources') etc
            const char* moduleRegistration = R"JS(
                (function() {
                    // Hook console logging to redirect to REPL
                    const util = require('util');
                    const originalConsole = globalThis.console;
                    
                    globalThis.console = {
                        ...originalConsole,
                        log: (...args) => {
                            if (originalConsole.log) originalConsole.log(...args);
                            obs.internal.log('log', util.formatWithOptions({colors: true}, ...args));
                        },
                        info: (...args) => {
                             if (originalConsole.info) originalConsole.info(...args);
                            obs.internal.log('info', util.formatWithOptions({colors: true}, ...args));
                        },
                        warn: (...args) => {
                             if (originalConsole.warn) originalConsole.warn(...args);
                            obs.internal.log('warn', util.formatWithOptions({colors: true}, ...args));
                        },
                        error: (...args) => {
                             if (originalConsole.error) originalConsole.error(...args);
                            obs.internal.log('error', util.formatWithOptions({colors: true}, ...args));
                        }
                    };

                    // Make require available globally for REPL
                    if (typeof require !== 'undefined') {
                        globalThis.require = require;
                    }
                    
                    // Create obs module exports
                    const obsModules = {
                        'obs': globalThis.obs,
                        'obs:sources': globalThis.obs?.sources,
                        'obs:scenes': globalThis.obs?.scenes,
                        'obs:sceneItems': globalThis.obs?.sceneItems,
                        'obs:filters': globalThis.obs?.filters,
                        'obs:transitions': globalThis.obs?.transitions,
                        'obs:frontend': globalThis.obs?.frontend,
                        'obs:canvas': globalThis.obs?.canvas,
                        'obs:events': globalThis.obs?.events,
                        'obs:modules': globalThis.obs?.modules,
                        'obs:outputs': globalThis.obs?.outputs,
                        'obs:encoders': globalThis.obs?.encoders,
                        'obs:services': globalThis.obs?.services,
                        'obs:data': globalThis.obs?.data,
                        'obs:properties': globalThis.obs?.properties,
                        'obs:audio': globalThis.obs?.audio,
                        'obs:hotkeys': globalThis.obs?.hotkeys
                    };
                    
                    // Store original require
                    const originalRequire = globalThis.require;
                    
                    // Create wrapped require that handles obs: prefix
                    if (originalRequire) {
                        globalThis.require = function(id) {
                            if (id === 'obs' || id.startsWith('obs:')) {
                                const mod = obsModules[id];
                                if (mod) return mod;
                                throw new Error(`Unknown OBS module: ${id}`);
                            }
                            return originalRequire(id);
                        };
                        // Preserve require properties
                        Object.assign(globalThis.require, originalRequire);
                    }
                })();
            )JS";
            
            v8::Local<v8::String> modSource = 
                v8::String::NewFromUtf8(isolate, moduleRegistration).ToLocalChecked();
            v8::Local<v8::Script> modScript;
            if (v8::Script::Compile(g_setup->context(), modSource).ToLocal(&modScript)) {
                modScript->Run(g_setup->context());
                obs_log(LOG_INFO, "OBS module resolver registered");
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