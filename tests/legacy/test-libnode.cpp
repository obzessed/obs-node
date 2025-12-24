/**
 * test-libnode.cpp - Standalone test executable for libnode integration
 * 
 * Tests script execution in a single shared Node.js environment.
 */

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

// Node.js headers
#include <node/node.h>
#include <node/uv.h>
#include <node/v8.h>

#define ISOLATE_THREAD_POOL_SIZE 4

// Global Node.js state (single environment)
static std::unique_ptr<node::MultiIsolatePlatform> g_platform;
static std::unique_ptr<node::CommonEnvironmentSetup> g_setup;
static std::shared_ptr<node::InitializationResult> g_init_result;

/**
 * Initialize Node.js runtime
 */
bool init_nodejs() {
    printf("Initializing Node.js...\n");

    std::vector<std::string> args = {"test-libnode"};

    g_init_result = node::InitializeOncePerProcess(args, {
        node::ProcessInitializationFlags::kNoInitializeV8,
        node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
        node::ProcessInitializationFlags::kNoPrintHelpOrVersionOutput
    });

    if (g_init_result->early_return() != 0) {
        fprintf(stderr, "Failed to initialize Node.js\n");
        for (const std::string& error : g_init_result->errors()) {
            fprintf(stderr, "  Error: %s\n", error.c_str());
        }
        return false;
    }

    g_platform = node::MultiIsolatePlatform::Create(ISOLATE_THREAD_POOL_SIZE);
    v8::V8::InitializePlatform(g_platform.get());
    v8::V8::Initialize();

    // Create single shared environment
    std::vector<std::string> errors;
    g_setup = node::CommonEnvironmentSetup::Create(
        g_platform.get(),
        &errors,
        g_init_result->args(),
        g_init_result->exec_args()
    );

    if (!g_setup) {
        fprintf(stderr, "Failed to create Node.js environment\n");
        for (const std::string& err : errors) {
            fprintf(stderr, "  Error: %s\n", err.c_str());
        }
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
        node::TearDownOncePerProcess();
        return false;
    }

    printf("Node.js initialized successfully!\n");
    return true;
}

/**
 * Shutdown Node.js runtime
 */
void shutdown_nodejs() {
    printf("\nShutting down Node.js...\n");
    
    if (g_setup) {
        v8::Isolate* isolate = g_setup->isolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolate_scope(isolate);
            node::Stop(g_setup->env());
        }
        g_setup.reset();
    }
    
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    g_platform.reset();
    g_init_result.reset();
    node::TearDownOncePerProcess();
}

/**
 * Run a script in the shared environment
 */
struct ScriptResult {
    bool success = false;
    std::string error;
    std::string output;
};

ScriptResult run_script(const std::string& script_code) {
    ScriptResult result;
    
    if (!g_setup) {
        result.error = "Node.js not initialized";
        return result;
    }

    v8::Isolate* isolate = g_setup->isolate();

    {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Context::Scope context_scope(g_setup->context());

        v8::TryCatch try_catch(isolate);

        // Compile
        v8::Local<v8::String> source = 
            v8::String::NewFromUtf8(isolate, script_code.c_str()).ToLocalChecked();
        
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(g_setup->context(), source).ToLocal(&script)) {
            if (try_catch.HasCaught()) {
                v8::Local<v8::Value> exception = try_catch.Exception();
                v8::String::Utf8Value utf8(isolate, exception);
                result.error = *utf8 ? *utf8 : "Compile error";
            } else {
                result.error = "Failed to compile script";
            }
            return result;
        }

        // Run
        v8::Local<v8::Value> script_result;
        if (!script->Run(g_setup->context()).ToLocal(&script_result)) {
            if (try_catch.HasCaught()) {
                v8::Local<v8::Value> exception = try_catch.Exception();
                v8::String::Utf8Value utf8(isolate, exception);
                result.error = *utf8 ? *utf8 : "Runtime error";
            } else {
                result.error = "Script execution failed";
            }
            return result;
        }

        // Get result as string
        v8::String::Utf8Value utf8(isolate, script_result);
        result.output = *utf8 ? *utf8 : "";
        
        // Drain any pending tasks
        uv_run(g_setup->event_loop(), UV_RUN_NOWAIT);
        g_platform->DrainTasks(isolate);
        
        result.success = true;
    }

    return result;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    printf("=== libnode Single Environment Test ===\n");
    printf("Node.js version: %s\n\n", NODE_VERSION_STRING);

    if (!init_nodejs()) {
        return 1;
    }

    // Test 1: Simple expression
    printf("\n--- Test 1: Simple expression ---\n");
    {
        auto result = run_script("1 + 2 + 3");
        printf("  Result: %s\n", result.success ? "SUCCESS" : "FAILED");
        if (result.success) {
            printf("  Output: %s\n", result.output.c_str());
        } else {
            printf("  Error: %s\n", result.error.c_str());
        }
    }

    // Test 2: Console.log (output goes to stdout)
    printf("\n--- Test 2: console.log ---\n");
    {
        auto result = run_script(
            "console.log('Hello from Node.js!');"
            "console.log('Version:', process.version);"
            "'done'"
        );
        printf("  Result: %s\n", result.success ? "SUCCESS" : "FAILED");
        if (!result.success) {
            printf("  Error: %s\n", result.error.c_str());
        }
    }

    // Test 3: Access Node.js APIs
    printf("\n--- Test 3: Node.js APIs ---\n");
    {
        auto result = run_script(
            "const os = require('os');"
            "console.log('Platform:', os.platform());"
            "console.log('Arch:', os.arch());"
            "os.platform()"
        );
        printf("  Result: %s\n", result.success ? "SUCCESS" : "FAILED");
        if (result.success) {
            printf("  Platform: %s\n", result.output.c_str());
        } else {
            printf("  Error: %s\n", result.error.c_str());
        }
    }

    // Test 4: Error handling
    printf("\n--- Test 4: Error handling ---\n");
    {
        auto result = run_script("throw new Error('Test error!')");
        printf("  Result: %s (expected FAILED)\n", result.success ? "SUCCESS" : "FAILED");
        if (!result.success) {
            printf("  Error: %s\n", result.error.c_str());
        }
    }

    // Test 5: Syntax error
    printf("\n--- Test 5: Syntax error ---\n");
    {
        auto result = run_script("function { broken syntax");
        printf("  Result: %s (expected FAILED)\n", result.success ? "SUCCESS" : "FAILED");
        if (!result.success) {
            printf("  Error: %s\n", result.error.c_str());
        }
    }

    // Test 6: Multiple consecutive scripts (shared state)
    printf("\n--- Test 6: Shared state between scripts ---\n");
    {
        run_script("globalThis.myCounter = 0;");
        run_script("globalThis.myCounter++;");
        run_script("globalThis.myCounter++;");
        auto result = run_script("globalThis.myCounter");
        printf("  Counter after 2 increments: %s\n", result.output.c_str());
        printf("  Result: %s\n", result.success && result.output == "2" ? "SUCCESS" : "FAILED");
    }

    shutdown_nodejs();

    printf("\n=== All Tests Complete ===\n");
    return 0;
}
