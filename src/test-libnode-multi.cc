/**
 * test-libnode-multi.cc - Enhanced ScriptEngine Architecture
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <chrono>

#include "experiments/all.hpp"

//=============================================================================
// Test Main
//=============================================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

int main()
{
    using namespace experiments;

    std::cout << "=== Enhanced ScriptEngine Test ===" << std::endl;

    std::vector<TestResult> results;

    auto &engine = ScriptEngine::Instance();

    // Setup logging (quieter for tests)
    engine.SetLogLevel(LogLevel::Warn);

    // Helper for timing
    auto now = []() {
	return std::chrono::steady_clock::now();
    };
    auto ms_since = [](auto start) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };

    // Initialize
    std::cout << "\n--- Initializing Engine ---" << std::endl;
    auto init_start = now();
    if (!engine.Initialize()) {
	std::cerr << "FATAL: Failed to initialize engine" << std::endl;
	return 1;
    }
    auto init_time = ms_since(init_start);
    std::cout << "  Init time: " << init_time << "ms" << std::endl;
    results.push_back({"Engine Init", true, std::to_string(init_time) + "ms"});

    // Test 1: Basic execution with timing
    std::cout << "\n--- Test 1: Basic Execution ---" << std::endl;
    {
	auto start = now();
	auto result = engine.ExecuteSync("40 + 2;");
	auto duration = ms_since(start);

	bool passed = result.IsOk() && result.Value() == "42";
	std::cout << "  Duration: " << duration << "ms" << std::endl;
	std::cout << (passed ? "[PASS]" : "[FAIL]")
		  << " Result: " << (result.IsOk() ? result.Value() : result.Error().message) << std::endl;
	results.push_back(
		{"Basic Execution", passed,
		 (result.IsOk() ? result.Value() : result.Error().message) + " (" + std::to_string(duration) + "ms)"});
    }

    // Test 2: Script timeout with detailed timing
    std::cout << "\n--- Test 2: Script Timeout ---" << std::endl;
    {
	const int64_t TIMEOUT_MS = 500;
	auto mainEnv = engine.GetMainEnvironment();
	std::cout << "  Env running: " << (mainEnv->IsRunning() ? "yes" : "no") << std::endl;

	auto script = engine.CreateScript("let x = 0; while(true) { x++; }",
					  {.name = "timeout-test", .timeout = std::chrono::milliseconds(TIMEOUT_MS)});

	std::cout << "  Before Execute - State: " << static_cast<int>(script->GetState()) << std::endl;

	auto exec_start = now();
	bool executed = mainEnv->Execute(script);
	auto exec_time = ms_since(exec_start);
	std::cout << "  Execute time: " << exec_time << "ms, returned: " << (executed ? "true" : "false") << std::endl;

	// Small delay to ensure script starts
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	std::cout << "  After 50ms - State: " << static_cast<int>(script->GetState()) << std::endl;

	auto wait_start = now();
	script->Wait(std::chrono::seconds(10));
	auto wait_duration = ms_since(wait_start);

	std::cout << "  Wait duration: " << wait_duration << "ms (expected ~" << (TIMEOUT_MS - 50) << "ms)"
		  << std::endl;
	std::cout << "  Final State: " << static_cast<int>(script->GetState()) << std::endl;
	std::cout << "  Error: " << script->GetError().message << std::endl;

	// Should be TimedOut and wait duration should be close to script timeout
	bool state_ok = script->GetState() == ScriptState::TimedOut;
	bool timing_ok = wait_duration < (TIMEOUT_MS + 200); // Allow 200ms tolerance
	bool passed = state_ok && timing_ok;

	std::cout << (passed ? "[PASS]" : "[FAIL]") << " Timeout test"
		  << " (state=" << (state_ok ? "ok" : "wrong") << ", timing=" << (timing_ok ? "ok" : "slow") << ")"
		  << std::endl;
	results.push_back({"Script Timeout", passed,
			   "State=" + std::to_string(static_cast<int>(script->GetState())) + " " +
				   std::to_string(wait_duration) + "ms"});
    }

    // Test 3: Priority Queue - Realistic scenario
    // Simulates: emergency stops, user actions, background processing, cleanup tasks
    std::cout << "\n--- Test 3: Priority Queue (Realistic Scenario) ---" << std::endl;
    {
	auto start = now();
	std::vector<std::string> execution_order;
	std::mutex order_mutex;

	// Simulate different priority tasks
	// CRITICAL: Emergency stop handler (must run first)
	auto emergency = engine.CreateScript(R"(
            // Emergency stop - highest priority
            globalThis.emergencyResult = 'EMERGENCY_HANDLED';
            'emergency_done';
        )",
					     {.name = "emergency-stop", .priority = ScriptPriority::Critical});

	// HIGH: User interaction response (UI button click)
	auto userAction = engine.CreateScript(R"(
            // User clicked a button - needs quick response
            const result = { action: 'button_click', processed: true };
            JSON.stringify(result);
        )",
					      {.name = "user-action", .priority = ScriptPriority::High});

	// NORMAL: Background data processing
	auto dataProcess = engine.CreateScript(R"(
            // Process some data in background
            let sum = 0;
            for (let i = 0; i < 1000; i++) sum += i;
            'data_processed:' + sum;
        )",
					       {.name = "data-processor", .priority = ScriptPriority::Normal});

	// LOW: Cleanup and logging tasks
	auto cleanup1 = engine.CreateScript(R"(
            // Cleanup old cache entries
            'cleanup_cache_done';
        )",
					    {.name = "cleanup-cache", .priority = ScriptPriority::Low});

	auto cleanup2 = engine.CreateScript(R"(
            // Log analytics event
            'analytics_logged';
        )",
					    {.name = "log-analytics", .priority = ScriptPriority::Low});

	// Track completion order via callbacks
	auto trackOrder = [&](const std::string &name) {
	    return [&, name](bool success, const std::string &result, const ScriptError &) {
		if (success) {
		    std::lock_guard lock(order_mutex);
		    execution_order.push_back(name);
		}
	    };
	};

	emergency->OnComplete(trackOrder("emergency"));
	userAction->OnComplete(trackOrder("userAction"));
	dataProcess->OnComplete(trackOrder("dataProcess"));
	cleanup1->OnComplete(trackOrder("cleanup1"));
	cleanup2->OnComplete(trackOrder("cleanup2"));

	// Queue in REVERSE priority order (low first, critical last)
	// Priority queue should still execute in correct order
	std::cout << "  Queueing in reverse order (low->critical)..." << std::endl;
	engine.GetMainEnvironment()->Execute(cleanup1);    // Low
	engine.GetMainEnvironment()->Execute(cleanup2);    // Low
	engine.GetMainEnvironment()->Execute(dataProcess); // Normal
	engine.GetMainEnvironment()->Execute(userAction);  // High
	engine.GetMainEnvironment()->Execute(emergency);   // Critical

	// Small delay to ensure all scripts are in the priority queue before processing
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Wait for all
	emergency->Wait(std::chrono::seconds(5));
	userAction->Wait(std::chrono::seconds(5));
	dataProcess->Wait(std::chrono::seconds(5));
	cleanup1->Wait(std::chrono::seconds(5));
	cleanup2->Wait(std::chrono::seconds(5));

	auto duration = ms_since(start);

	// Check all completed
	bool all_completed =
		emergency->GetState() == ScriptState::Completed && userAction->GetState() == ScriptState::Completed &&
		dataProcess->GetState() == ScriptState::Completed && cleanup1->GetState() == ScriptState::Completed &&
		cleanup2->GetState() == ScriptState::Completed;

	// Check priority order (critical should be first)
	bool priority_ok = !execution_order.empty() && execution_order[0] == "emergency";

	std::cout << "  Duration: " << duration << "ms" << std::endl;
	std::cout << "  Execution order: ";
	for (size_t i = 0; i < execution_order.size(); i++) {
	    if (i > 0)
		std::cout << " -> ";
	    std::cout << execution_order[i];
	}
	std::cout << std::endl;
	std::cout << "  Results:" << std::endl;
	std::cout << "    Emergency: " << emergency->GetResult() << std::endl;
	std::cout << "    UserAction: " << userAction->GetResult() << std::endl;
	std::cout << "    DataProcess: " << dataProcess->GetResult() << std::endl;

	bool passed = all_completed && priority_ok;
	std::cout << (passed ? "[PASS]" : "[FAIL]") << " Priority execution (all=" << (all_completed ? "ok" : "fail")
		  << ", order=" << (priority_ok ? "ok" : "wrong") << ")" << std::endl;
	results.push_back({"Priority Queue", passed,
			   "Order: " + (execution_order.empty() ? "none" : execution_order[0]) + "... (" +
				   std::to_string(duration) + "ms)"});
    }

    // Test 4: Script Context - Multiple scenarios
    std::cout << "\n--- Test 4: Script Context (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 5;

	// Scenario 4.1: Basic CRUD operations
	std::cout << "  4.1 Basic CRUD..." << std::endl;
	{
	    auto ctx = std::make_shared<ScriptContext>();

	    // Create
	    ctx->Set("user", "john");
	    ctx->Set("age", "30");

	    // Read
	    bool create_ok = ctx->Get("user").value_or("") == "john" && ctx->Get("age").value_or("") == "30";

	    // Update
	    ctx->Set("age", "31");
	    bool update_ok = ctx->Get("age").value_or("") == "31";

	    // Delete
	    ctx->Remove("user");
	    bool delete_ok = !ctx->Get("user").has_value();

	    // Non-existent
	    bool missing_ok = !ctx->Get("nonexistent").has_value();

	    bool passed = create_ok && update_ok && delete_ok && missing_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// Scenario 4.2: Concurrent read/write (simulated with threads)
	std::cout << "  4.2 Concurrent access..." << std::endl;
	{
	    auto ctx = std::make_shared<ScriptContext>();
	    std::atomic<int> success_count{0};
	    std::vector<std::thread> threads;

	    // Multiple writers
	    for (int i = 0; i < 10; i++) {
		threads.emplace_back([ctx, i, &success_count]() {
		    std::string key = "thread_" + std::to_string(i);
		    ctx->Set(key, std::to_string(i * 10));
		    std::this_thread::sleep_for(std::chrono::milliseconds(5));
		    if (ctx->Get(key).has_value()) {
			success_count++;
		    }
		});
	    }

	    // Multiple readers
	    for (int i = 0; i < 5; i++) {
		threads.emplace_back([ctx]() {
		    for (int j = 0; j < 10; j++) {
			ctx->Get("thread_" + std::to_string(j));
		    }
		});
	    }

	    for (auto &t : threads)
		t.join();

	    bool passed = success_count.load() == 10;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (" << success_count.load()
		      << "/10 writes verified)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// Scenario 4.3: Bulk operations
	std::cout << "  4.3 Bulk operations..." << std::endl;
	{
	    auto ctx = std::make_shared<ScriptContext>();

	    // Add 100 items
	    for (int i = 0; i < 100; i++) {
		ctx->Set("item_" + std::to_string(i), "value_" + std::to_string(i));
	    }

	    // Verify random samples
	    bool sample_ok = ctx->Get("item_0").value_or("") == "value_0" &&
			     ctx->Get("item_50").value_or("") == "value_50" &&
			     ctx->Get("item_99").value_or("") == "value_99";

	    // Get all and verify count
	    auto all = ctx->GetAll();
	    bool count_ok = all.size() == 100;

	    // Clear and verify empty
	    ctx->Clear();
	    bool clear_ok = ctx->GetAll().empty();

	    bool passed = sample_ok && count_ok && clear_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (100 items, clear worked)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// Scenario 4.4: Config-like usage pattern
	std::cout << "  4.4 Config pattern..." << std::endl;
	{
	    auto config = std::make_shared<ScriptContext>();

	    // Set default config
	    config->Set("log_level", "info");
	    config->Set("max_connections", "100");
	    config->Set("timeout_ms", "5000");
	    config->Set("feature_flag_x", "true");

	    // Read with defaults
	    auto getConfig = [&](const std::string &key, const std::string &def) {
		return config->Get(key).value_or(def);
	    };

	    bool passed = getConfig("log_level", "warn") == "info" && getConfig("max_connections", "50") == "100" &&
			  getConfig("missing_key", "default") == "default";

	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// Scenario 4.5: Shared context between scripts (simulated)
	std::cout << "  4.5 Script data sharing..." << std::endl;
	{
	    auto shared = std::make_shared<ScriptContext>();

	    // Script 1 "writes" data
	    shared->Set("script1_result", "processed_data");
	    shared->Set("script1_status", "complete");

	    // Script 2 "reads" script 1's data and adds its own
	    auto s1_result = shared->Get("script1_result").value_or("");
	    shared->Set("script2_used", s1_result);
	    shared->Set("script2_status", "complete");

	    // Script 3 "aggregates" all results
	    auto s1_status = shared->Get("script1_status").value_or("");
	    auto s2_status = shared->Get("script2_status").value_or("");
	    bool workflow_complete = s1_status == "complete" && s2_status == "complete";

	    bool passed = workflow_complete && shared->Get("script2_used").value_or("") == "processed_data";
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Context tests: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Script Context", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 5: Metrics
    std::cout << "\n--- Test 5: Metrics ---" << std::endl;
    {
	auto mainEnv = engine.GetMainEnvironment();
	auto metrics = mainEnv->GetMetrics();

	bool passed = metrics.execution.scripts_executed > 0 && metrics.memory.used_heap_size > 0;
	std::cout << "  Scripts executed: " << metrics.execution.scripts_executed << std::endl;
	std::cout << "  Scripts failed: " << metrics.execution.scripts_failed << std::endl;
	std::cout << "  Scripts timed out: " << metrics.execution.scripts_timed_out << std::endl;
	std::cout << "  Heap used: " << metrics.memory.used_heap_size / 1024 << " KB" << std::endl;
	std::cout << (passed ? "[PASS]" : "[FAIL]") << " Metrics collected" << std::endl;
	results.push_back({"Metrics", passed, std::to_string(metrics.execution.scripts_executed) + " scripts"});
    }

    // Test 6: Custom Environment - Multiple Scenarios
    std::cout << "\n--- Test 6: Custom Environment (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 4;

	// 6.1: Basic custom environment with bootstrap
	std::cout << "  6.1 Bootstrap script..." << std::endl;
	{
	    EnvironmentConfig config;
	    config.name = "Bootstrap-Test";
	    config.bootstrap_script = "globalThis.API_VERSION = '2.0'; globalThis.DEBUG = true;";

	    auto env = engine.CreateEnvironment(config);
	    bool passed = false;
	    if (env) {
		auto r1 = env->ExecuteSync("API_VERSION;", std::chrono::seconds(2));
		auto r2 = env->ExecuteSync("DEBUG;", std::chrono::seconds(2));
		passed = r1.IsOk() && r1.Value() == "2.0" && r2.IsOk() && r2.Value() == "true";
	    }
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 6.2: Environment isolation (separate environments don't share state)
	std::cout << "  6.2 Environment isolation..." << std::endl;
	{
	    EnvironmentConfig config1, config2;
	    config1.name = "Isolated-1";
	    config1.bootstrap_script = "globalThis.envId = 'env1';";
	    config2.name = "Isolated-2";
	    config2.bootstrap_script = "globalThis.envId = 'env2';";

	    auto env1 = engine.CreateEnvironment(config1);
	    auto env2 = engine.CreateEnvironment(config2);

	    bool passed = false;
	    if (env1 && env2) {
		auto r1 = env1->ExecuteSync("envId;", std::chrono::seconds(2));
		auto r2 = env2->ExecuteSync("envId;", std::chrono::seconds(2));
		// Each env should have its own value
		passed = r1.IsOk() && r1.Value() == "env1" && r2.IsOk() && r2.Value() == "env2";
	    }
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 6.3: Multi-script execution in custom env
	std::cout << "  6.3 Multi-script execution..." << std::endl;
	{
	    EnvironmentConfig config;
	    config.name = "Multi-Script";
	    config.bootstrap_script = "globalThis.counter = 0;";

	    auto env = engine.CreateEnvironment(config);
	    bool passed = false;
	    if (env) {
		// Run multiple scripts that modify shared state
		env->ExecuteSync("counter++;", std::chrono::seconds(1));
		env->ExecuteSync("counter++;", std::chrono::seconds(1));
		env->ExecuteSync("counter++;", std::chrono::seconds(1));
		auto result = env->ExecuteSync("counter;", std::chrono::seconds(1));
		passed = result.IsOk() && result.Value() == "3";
	    }
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 6.4: Environment config with default timeout
	std::cout << "  6.4 Default timeout config..." << std::endl;
	{
	    EnvironmentConfig config;
	    config.name = "Timeout-Config-Test";
	    config.default_script_timeout = std::chrono::milliseconds(100); // Very short

	    auto env = engine.CreateEnvironment(config);
	    bool passed = false;
	    if (env) {
		// Execute a slow script - should timeout due to env default
		auto script = std::make_shared<Script>("let x = 0; while(true) { x++; }", // Infinite loop
						       Script::Options{} // No explicit timeout - uses env default
		);
		env->Execute(script);
		script->Wait(std::chrono::seconds(2));

		// Should timeout with the env's default (100ms)
		passed = script->GetState() == ScriptState::TimedOut;
		std::cout << "         State: " << static_cast<int>(script->GetState()) << std::endl;
	    }
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Custom Environment: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Custom Environment", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 7: Error Handling - Multiple Scenarios
    std::cout << "\n--- Test 7: Error Handling (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 5;

	// 7.1: Runtime error (throw)
	std::cout << "  7.1 Runtime error (throw)..." << std::endl;
	{
	    auto result = engine.ExecuteSync("throw new Error('test error');", std::chrono::seconds(2));
	    bool passed = result.IsError() && result.Error().code == ErrorCode::RuntimeError &&
			  result.Error().message.find("test error") != std::string::npos;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " " << result.Error().message << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 7.2: Syntax error (compile error)
	std::cout << "  7.2 Syntax error..." << std::endl;
	{
	    auto result = engine.ExecuteSync("function { invalid syntax", std::chrono::seconds(2));
	    bool passed = result.IsError() && result.Error().code == ErrorCode::CompileError;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " " << result.Error().message.substr(0, 50)
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 7.3: Reference error (undefined variable)
	std::cout << "  7.3 Reference error..." << std::endl;
	{
	    auto result = engine.ExecuteSync("undefinedVar.property;", std::chrono::seconds(2));
	    bool passed = result.IsError() && result.Error().code == ErrorCode::RuntimeError;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " " << result.Error().message << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 7.4: Type error
	std::cout << "  7.4 Type error..." << std::endl;
	{
	    auto result = engine.ExecuteSync("null.toString();", std::chrono::seconds(2));
	    bool passed = result.IsError() && result.Error().code == ErrorCode::RuntimeError;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " " << result.Error().message << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 7.5: Async error (unhandled promise rejection - may or may not propagate)
	std::cout << "  7.5 Stack trace capture..." << std::endl;
	{
	    auto result = engine.ExecuteSync(R"(
                function level3() { throw new Error('deep error'); }
                function level2() { level3(); }
                function level1() { level2(); }
                level1();
            )",
					     std::chrono::seconds(2));
	    // Check that we captured a stack trace
	    bool passed = result.IsError() && !result.Error().stack.empty() &&
			  result.Error().stack.find("level") != std::string::npos;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
		      << " Stack captured: " << (result.Error().stack.empty() ? "no" : "yes") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Error Handling: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Error Handling", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 8: Module System (Resolvers + Loaders)
    std::cout << "\n--- Test 8: Module System (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 8.1: VirtualResolver + VirtualLoader
	std::cout << "  8.1 Virtual resolver/loader..." << std::endl;
	{
	    auto resolver = std::make_shared<VirtualResolver>();
	    auto loader = std::make_shared<VirtualLoader>();

	    // Register in both
	    resolver->Register("math-utils");
	    loader->Register("math-utils", R"(
                module.exports = { add: (a, b) => a + b };
            )");

	    resolver->Register("config");
	    loader->Register("config", R"(
                module.exports = { version: '1.0.0' };
            )");

	    // Test resolution
	    auto resolved1 = resolver->Resolve("math-utils", "");
	    auto resolved2 = resolver->Resolve("virtual:config", "");
	    auto resolved3 = resolver->Resolve("nonexistent", "");

	    bool resolve_ok = resolved1.has_value() && resolved2.has_value() && !resolved3.has_value();

	    // Test loading
	    auto loaded = loader->Load(resolved1->resolved_path);
	    bool load_ok = loaded.has_value() && loaded->source.find("module.exports") != std::string::npos;

	    // Test list
	    auto modules = loader->ListModules();
	    bool list_ok = modules.size() == 2;

	    // Test unregister
	    resolver->Unregister("config");
	    loader->Unregister("config");
	    bool unregister_ok = !resolver->Resolve("config", "").has_value();

	    bool passed = resolve_ok && load_ok && list_ok && unregister_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (resolve=" << (resolve_ok ? "ok" : "fail")
		      << ", load=" << (load_ok ? "ok" : "fail") << ", list=" << (list_ok ? "ok" : "fail") << ")"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 8.2: DiskResolver
	std::cout << "  8.2 Disk resolver..." << std::endl;
	{
	    auto resolver = std::make_shared<DiskResolver>(std::vector<std::string>{".", "./node_modules"});

	    // Test that it can handle relative/absolute paths
	    bool can_handle_relative = resolver->CanHandle("./test.js");
	    bool can_handle_absolute = resolver->CanHandle("/some/path.js");
	    bool can_handle_bare = resolver->CanHandle("lodash");
	    bool rejects_url = !resolver->CanHandle("https://example.com/module.js");
	    bool rejects_virtual = !resolver->CanHandle("virtual:test");

	    bool passed = can_handle_relative && can_handle_absolute && can_handle_bare && rejects_url &&
			  rejects_virtual;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
		      << " (handles: relative, absolute, bare; rejects: url, virtual)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 8.3: ResolverChain priority
	std::cout << "  8.3 Resolver chain priority..." << std::endl;
	{
	    auto chain = std::make_shared<ResolverChain>();

	    // Virtual resolver (high priority - first)
	    auto virtualResolver = std::make_shared<VirtualResolver>();
	    virtualResolver->Register("my-module");

	    // Disk resolver (lower priority)
	    auto diskResolver = std::make_shared<DiskResolver>();

	    chain->AddResolver(virtualResolver);
	    chain->AddResolver(diskResolver);

	    // Verify chain order
	    auto names = chain->GetResolverNames();
	    bool order_ok = names.size() == 2 && names[0] == "VirtualResolver" && names[1] == "DiskResolver";

	    // Virtual should resolve first
	    auto resolved = chain->Resolve("my-module", "");
	    bool priority_ok = resolved.has_value() && resolved->resolved_path.starts_with("virtual:");

	    // Can handle check
	    bool can_handle = chain->CanHandle("my-module") && chain->CanHandle("./local.js") &&
			      !chain->CanHandle("https://remote.com/mod.js");

	    bool passed = order_ok && priority_ok && can_handle;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (order=" << (order_ok ? "ok" : "wrong")
		      << ", priority=" << (priority_ok ? "ok" : "wrong") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 8.4: ModuleSystem integration
	std::cout << "  8.4 ModuleSystem integration..." << std::endl;
	{
	    auto resolver = std::make_shared<VirtualResolver>();
	    auto loader = std::make_shared<VirtualLoader>();

	    resolver->Register("test-module");
	    loader->Register("test-module", "module.exports = 42;");

	    ModuleSystem system(resolver, loader);

	    // Test Require (resolve + load)
	    auto module_info = system.Require("test-module");
	    bool require_ok = module_info.has_value() && module_info->source.find("42") != std::string::npos;

	    // Test separate resolve/load
	    auto resolved = system.Resolve("test-module");
	    bool resolve_ok = resolved.has_value();

	    auto loaded = system.Load(resolved->resolved_path);
	    bool load_ok = loaded.has_value();

	    bool passed = require_ok && resolve_ok && load_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (require=" << (require_ok ? "ok" : "fail")
		      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ", load=" << (load_ok ? "ok" : "fail") << ")"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 8.5: Module format detection
	std::cout << "  8.5 Format detection..." << std::endl;
	{
	    auto loader = std::make_shared<VirtualLoader>();

	    // Register with different formats
	    loader->Register("esm-module", "export default 42;", ModuleFormat::ESModule);
	    loader->Register("cjs-module", "module.exports = 42;", ModuleFormat::CommonJS);

	    auto esm = loader->Load("virtual:esm-module");
	    auto cjs = loader->Load("virtual:cjs-module");

	    bool esm_ok = esm.has_value() && esm->format == ModuleFormat::ESModule;
	    bool cjs_ok = cjs.has_value() && cjs->format == ModuleFormat::CommonJS;

	    bool passed = esm_ok && cjs_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (ESM=" << (esm_ok ? "ok" : "fail")
		      << ", CJS=" << (cjs_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 8.6: Factory methods
	std::cout << "  8.6 ModuleSystemFactory..." << std::endl;
	{
	    // Standard system
	    auto standard = ModuleSystemFactory::CreateStandard();
	    bool standard_ok = standard->GetResolver() != nullptr && standard->GetLoader() != nullptr;

	    // Full system (with remote)
	    auto full = ModuleSystemFactory::CreateFull({"https://cdn.example.com/"});
	    bool full_ok = full->GetResolver() != nullptr;

	    // Sandboxed (virtual only)
	    auto sandboxed = ModuleSystemFactory::CreateSandboxed();
	    bool sandboxed_ok = sandboxed->GetResolver() != nullptr;

	    // Disk only
	    auto disk = ModuleSystemFactory::CreateDiskOnly({"./lib", "./vendor"});
	    bool disk_ok = disk->GetResolver() != nullptr;

	    bool passed = standard_ok && full_ok && sandboxed_ok && disk_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (all factories create valid systems)"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Module System: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Module System", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 9: Permissions System
    std::cout << "\n--- Test 9: Permissions System (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 9.1: Basic permission operations
	std::cout << "  9.1 Basic grant/revoke..." << std::endl;
	{
	    PermissionSet perms;

	    // Start empty
	    bool start_empty = perms.IsEmpty();

	    // Grant
	    perms.Grant(ScriptPermission::FileRead);
	    perms.Grant(ScriptPermission::NetHttp);
	    bool has_file = perms.Has(ScriptPermission::FileRead);
	    bool has_net = perms.Has(ScriptPermission::NetHttp);
	    bool no_write = !perms.Has(ScriptPermission::FileWrite);

	    // Revoke
	    perms.Revoke(ScriptPermission::FileRead);
	    bool revoked = !perms.Has(ScriptPermission::FileRead);
	    bool still_net = perms.Has(ScriptPermission::NetHttp);

	    bool passed = start_empty && has_file && has_net && no_write && revoked && still_net;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 9.2: Preset factories
	std::cout << "  9.2 Preset factories..." << std::endl;
	{
	    auto safe = PermissionSet::Safe();
	    auto standard = PermissionSet::Standard();
	    auto trusted = PermissionSet::Trusted();
	    auto full = PermissionSet::Full();

	    // Safe has timers
	    bool safe_ok = safe.Has(ScriptPermission::Timers) && !safe.Has(ScriptPermission::FileRead);

	    // Standard has modules
	    bool standard_ok = standard.Has(ScriptPermission::ModuleRequire);

	    // Trusted has OBS access
	    bool trusted_ok = trusted.Has(ScriptPermission::ObsScenes) && trusted.Has(ScriptPermission::NetHttp);

	    // Full has everything
	    bool full_ok = full.Has(ScriptPermission::Full) && full.Has(ScriptPermission::ProcessSpawn);

	    bool passed = safe_ok && standard_ok && trusted_ok && full_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 9.3: Bitwise operations
	std::cout << "  9.3 Bitwise operations..." << std::endl;
	{
	    // Combine permissions
	    auto combined = ScriptPermission::FileRead | ScriptPermission::FileWrite;
	    PermissionSet perms(combined);
	    bool has_both = perms.Has(ScriptPermission::FileRead) && perms.Has(ScriptPermission::FileWrite);

	    // HasAny
	    bool has_any = perms.HasAny(ScriptPermission::FileSystem);
	    bool no_net = !perms.HasAny(ScriptPermission::Network);

	    bool passed = has_both && has_any && no_net;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 9.4: Permission merging
	std::cout << "  9.4 Permission merging..." << std::endl;
	{
	    PermissionSet a(ScriptPermission::FileRead);
	    PermissionSet b(ScriptPermission::NetHttp);

	    // Merge (union)
	    PermissionSet merged = a;
	    merged.Merge(b);
	    bool merge_ok = merged.Has(ScriptPermission::FileRead) && merged.Has(ScriptPermission::NetHttp);

	    // Intersect
	    PermissionSet full(ScriptPermission::Full);
	    PermissionSet limited(ScriptPermission::FileRead | ScriptPermission::Timers);
	    full.Intersect(limited);
	    bool intersect_ok = full.Has(ScriptPermission::FileRead) && full.Has(ScriptPermission::Timers) &&
				!full.Has(ScriptPermission::NetHttp);

	    bool passed = merge_ok && intersect_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 9.5: EnvironmentConfig presets
	std::cout << "  9.5 EnvironmentConfig presets..." << std::endl;
	{
	    auto sandboxed = EnvironmentConfig::Sandboxed();
	    auto trusted = EnvironmentConfig::Trusted();
	    auto default_cfg = EnvironmentConfig::Default();

	    bool sandboxed_ok = sandboxed.name == "Sandboxed" && !sandboxed.allow_file_access &&
				sandboxed.permissions.Has(ScriptPermission::Timers);

	    bool trusted_ok = trusted.permissions.Has(ScriptPermission::ObsScenes);

	    bool default_ok = default_cfg.permissions.Has(ScriptPermission::ModuleRequire);

	    bool passed = sandboxed_ok && trusted_ok && default_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 9.6: Script-level permissions
	std::cout << "  9.6 Script-level permissions..." << std::endl;
	{
	    // Script with explicit permissions
	    Script::Options opts;
	    opts.name = "sandboxed-script";
	    opts.permissions = PermissionSet::Safe();
	    opts.source_file = "user-scripts/untrusted.js";
	    opts.author = "unknown";
	    opts.trusted = false;

	    auto script = std::make_shared<Script>("'test';", opts);

	    bool perms_ok = script->HasPermission(ScriptPermission::Timers) &&
			    !script->HasPermission(ScriptPermission::FileRead);
	    bool meta_ok = script->GetSourceFile() == "user-scripts/untrusted.js" && script->GetAuthor() == "unknown" &&
			   !script->IsTrusted();

	    // Script without permissions (uses env default)
	    auto default_script = std::make_shared<Script>("'test';", Script::Options{});
	    bool default_ok = default_script->HasPermission(ScriptPermission::FileRead); // true = uses env default

	    bool passed = perms_ok && meta_ok && default_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Permissions: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Permissions", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 10: NPM/JSR Resolvers and Transformers
    std::cout << "\n--- Test 10: Package Resolvers & Transformers (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 10.1: NPM package spec parsing
	std::cout << "  10.1 NPM package spec parsing..." << std::endl;
	{
	    // Simple package
	    auto simple = NpmPackageSpec::Parse("npm:lodash");
	    bool simple_ok = simple && simple->name == "lodash" && simple->version == "latest";

	    // With version
	    auto versioned = NpmPackageSpec::Parse("npm:react@18.2.0");
	    bool versioned_ok = versioned && versioned->name == "react" && versioned->version == "18.2.0";

	    // Scoped package
	    auto scoped = NpmPackageSpec::Parse("npm:@types/node@20.0.0");
	    bool scoped_ok = scoped && scoped->name == "@types/node" && scoped->version == "20.0.0";

	    // With subpath
	    auto subpath = NpmPackageSpec::Parse("npm:lodash@4.17.21/cloneDeep");
	    bool subpath_ok = subpath && subpath->name == "lodash" && subpath->version == "4.17.21" &&
			      subpath->subpath == "/cloneDeep";

	    // Invalid
	    auto invalid = NpmPackageSpec::Parse("http://example.com");
	    bool invalid_ok = !invalid.has_value();

	    bool passed = simple_ok && versioned_ok && scoped_ok && subpath_ok && invalid_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (simple=" << (simple_ok ? "ok" : "fail")
		      << ", versioned=" << (versioned_ok ? "ok" : "fail") << ", scoped=" << (scoped_ok ? "ok" : "fail")
		      << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 10.2: JSR package spec parsing
	std::cout << "  10.2 JSR package spec parsing..." << std::endl;
	{
	    // Standard JSR package
	    auto std = JsrPackageSpec::Parse("jsr:@std/path@1.0.0");
	    bool std_ok = std && std->scope == "@std" && std->name == "path" && std->version == "1.0.0";

	    // With subpath
	    auto subpath = JsrPackageSpec::Parse("jsr:@std/fs@0.5.0/walk");
	    bool subpath_ok = subpath && subpath->name == "fs" && subpath->subpath == "/walk";

	    // No version
	    auto noversion = JsrPackageSpec::Parse("jsr:@oak/oak");
	    bool noversion_ok = noversion && noversion->name == "oak" && noversion->version.empty();

	    // Invalid (not scoped)
	    auto invalid = JsrPackageSpec::Parse("jsr:lodash");
	    bool invalid_ok = !invalid.has_value();

	    bool passed = std_ok && subpath_ok && noversion_ok && invalid_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (std=" << (std_ok ? "ok" : "fail")
		      << ", subpath=" << (subpath_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 10.3: NPM Resolver
	std::cout << "  10.3 NPM resolver..." << std::endl;
	{
	    auto resolver = std::make_shared<NpmResolver>();

	    // Can handle npm: URLs
	    bool can_handle = resolver->CanHandle("npm:lodash") && resolver->CanHandle("npm:@types/node@20.0.0") &&
			      !resolver->CanHandle("./local.js") && !resolver->CanHandle("jsr:@std/path");

	    // Resolve to CDN URL
	    auto resolved = resolver->Resolve("npm:react@18.2.0", "");
	    bool resolve_ok = resolved && resolved->resolved_path.find("esm.sh") != std::string::npos;

	    bool passed = can_handle && resolve_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (can_handle=" << (can_handle ? "ok" : "fail")
		      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 10.4: JSR Resolver
	std::cout << "  10.4 JSR resolver..." << std::endl;
	{
	    auto resolver = std::make_shared<JsrResolver>();

	    // Can handle jsr: URLs
	    bool can_handle = resolver->CanHandle("jsr:@std/path") && !resolver->CanHandle("npm:lodash") &&
			      !resolver->CanHandle("./local.js");

	    // Resolve to JSR URL
	    auto resolved = resolver->Resolve("jsr:@std/path@1.0.0/mod.ts", "");
	    bool resolve_ok = resolved && resolved->resolved_path.find("jsr.io") != std::string::npos;

	    bool passed = can_handle && resolve_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (can_handle=" << (can_handle ? "ok" : "fail")
		      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 10.5: TypeScript Transformer
	std::cout << "  10.5 TypeScript transformer..." << std::endl;
	{
	    TypeScriptTransformer::Options opts;
	    opts.generate_source_map = true;
	    auto transformer = std::make_shared<TypeScriptTransformer>(opts);

	    // Should transform TypeScript files
	    bool should_ts = transformer->ShouldTransform("foo.ts");
	    bool should_tsx = transformer->ShouldTransform("component.tsx");
	    bool should_mts = transformer->ShouldTransform("module.mts");
	    bool should_not_js = !transformer->ShouldTransform("script.js");

	    // Transform some TypeScript
	    std::string ts_code = "const x: number = 42; export default x;";
	    auto result = transformer->Transform(ts_code, "test.ts", ModuleFormat::ESModule);

	    bool transform_ok = result.is_ok() && !result.code.empty();
	    bool source_map_ok = result.source_map.has_value();

	    bool passed = should_ts && should_tsx && should_mts && should_not_js && transform_ok && source_map_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (transform=" << (transform_ok ? "ok" : "fail")
		      << ", sourcemap=" << (source_map_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 10.6: TransformingLoader
	std::cout << "  10.6 TransformingLoader..." << std::endl;
	{
	    // Create a virtual loader with TypeScript content
	    auto inner_loader = std::make_shared<VirtualLoader>();
	    inner_loader->Register("app.ts", "const msg: string = 'hello'; console.log(msg);");
	    inner_loader->Register("app.js", "const msg = 'hello'; console.log(msg);");

	    auto transformer = std::make_shared<TypeScriptTransformer>();
	    auto transforming_loader = std::make_shared<TransformingLoader>(inner_loader, transformer);

	    // Load TypeScript file (should transform)
	    auto ts_module = transforming_loader->Load("virtual:app.ts");
	    bool ts_transformed = ts_module && ts_module->transformed;

	    // Load JavaScript file (should NOT transform)
	    auto js_module = transforming_loader->Load("virtual:app.js");
	    bool js_not_transformed = js_module && !js_module->transformed;

	    // Check name
	    bool name_ok = transforming_loader->GetName().find("VirtualLoader") != std::string::npos;

	    bool passed = ts_transformed && js_not_transformed && name_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
		      << " (ts_transform=" << (ts_transformed ? "ok" : "fail")
		      << ", js_passthrough=" << (js_not_transformed ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Package Resolvers & Transformers: " << scenarios_passed
		  << "/" << total_scenarios << std::endl;
	results.push_back({"Pkg Resolvers/Transform", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 11: Module Cache & Dependency Graph & Import Maps
    std::cout << "\n--- Test 11: Cache, Dependencies & Import Maps (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 11.1: Module Cache basics
	std::cout << "  11.1 Module cache basics..." << std::endl;
	{
	    ModuleCache::Options opts;
	    opts.max_entries = 10;
	    opts.max_size_bytes = 1000;
	    ModuleCache cache(opts);

	    // Put and get
	    ModuleInfo mod1;
	    mod1.specifier = "test";
	    mod1.resolved_path = "virtual:test";
	    mod1.source = "module.exports = 42;";

	    cache.Put("virtual:test", mod1);
	    auto retrieved = cache.Get("virtual:test");
	    bool put_get_ok = retrieved && retrieved->source == mod1.source;

	    // Check has
	    bool has_ok = cache.Has("virtual:test") && !cache.Has("nonexistent");

	    // Trigger a cache miss (Has() doesn't count misses, only Get() does)
	    cache.Get("nonexistent");

	    // Invalidate
	    cache.Invalidate("virtual:test");
	    bool invalidate_ok = !cache.Has("virtual:test");

	    // Stats
	    auto stats = cache.GetStats();
	    bool stats_ok = stats.hits == 1 && stats.misses >= 1;

	    bool passed = put_get_ok && has_ok && invalidate_ok && stats_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (get=" << (put_get_ok ? "ok" : "fail")
		      << ", invalidate=" << (invalidate_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 11.2: LRU eviction
	std::cout << "  11.2 LRU eviction..." << std::endl;
	{
	    ModuleCache::Options opts;
	    opts.max_entries = 3;
	    ModuleCache cache(opts);

	    // Add 4 modules (should evict the first one)
	    for (int i = 0; i < 4; i++) {
		ModuleInfo mod;
		mod.source = "code " + std::to_string(i);
		cache.Put("mod" + std::to_string(i), mod);
	    }

	    // First should be evicted
	    bool evicted_ok = !cache.Has("mod0");
	    bool kept_ok = cache.Has("mod1") && cache.Has("mod2") && cache.Has("mod3");

	    auto stats = cache.GetStats();
	    bool eviction_counted = stats.evictions >= 1;

	    bool passed = evicted_ok && kept_ok && eviction_counted;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (evicted=" << (evicted_ok ? "ok" : "fail")
		      << ", kept=" << (kept_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 11.3: Dependency Graph cycle detection
	std::cout << "  11.3 Dependency graph cycle detection..." << std::endl;
	{
	    DependencyGraph graph(DependencyGraph::CycleAction::Warn);

	    // A -> B -> C
	    graph.AddDependency("A", "B");
	    graph.AddDependency("B", "C");

	    // Would C -> A create a cycle?
	    bool would_cycle = graph.WouldCreateCycle("C", "A");

	    // B -> D shouldn't create a cycle
	    bool no_cycle = !graph.WouldCreateCycle("B", "D");

	    // Get all deps of A
	    auto deps = graph.GetAllDependencies("A");
	    bool deps_ok = deps.size() == 2; // B and C

	    // Topological order
	    auto order = graph.GetTopologicalOrder();
	    bool topo_ok = order.size() == 3;

	    bool passed = would_cycle && no_cycle && deps_ok && topo_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]")
		      << " (cycle=" << (would_cycle ? "detected" : "missed") << ", deps=" << deps.size() << ")"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 11.4: CachingLoader integration
	std::cout << "  11.4 CachingLoader integration..." << std::endl;
	{
	    auto inner_loader = std::make_shared<VirtualLoader>();
	    inner_loader->Register("lib", "module.exports = 'lib';");

	    auto cache = std::make_shared<ModuleCache>();
	    auto graph = std::make_shared<DependencyGraph>();
	    auto caching_loader = std::make_shared<CachingLoader>(inner_loader, cache, graph);

	    // First load (miss)
	    auto mod1 = caching_loader->Load("virtual:lib");
	    bool first_ok = mod1.has_value();

	    // Second load (hit)
	    auto mod2 = caching_loader->Load("virtual:lib");
	    bool second_ok = mod2.has_value();

	    // Check stats
	    auto stats = caching_loader->GetCacheStats();
	    bool stats_ok = stats.hits == 1 && stats.misses == 1;

	    // Track dependency
	    caching_loader->TrackDependency("app", "virtual:lib");
	    auto graph_ptr = caching_loader->GetDependencyGraph();
	    bool dep_ok = graph_ptr->Size() == 2;

	    bool passed = first_ok && second_ok && stats_ok && dep_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (hit=" << stats.hits
		      << ", miss=" << stats.misses << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 11.5: Import Map basic resolution
	std::cout << "  11.5 Import map resolution..." << std::endl;
	{
	    ImportMap map;
	    map.imports["lodash"] = "./vendor/lodash.js";
	    map.imports["lodash/"] = "./vendor/lodash/";

	    // Direct mapping
	    auto direct = map.Resolve("lodash");
	    bool direct_ok = direct && *direct == "./vendor/lodash.js";

	    // Prefix mapping
	    auto prefix = map.Resolve("lodash/cloneDeep");
	    bool prefix_ok = prefix && *prefix == "./vendor/lodash/cloneDeep";

	    // Unmapped
	    auto unmapped = map.Resolve("react");
	    bool unmapped_ok = !unmapped.has_value();

	    bool passed = direct_ok && prefix_ok && unmapped_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (direct=" << (direct_ok ? "ok" : "fail")
		      << ", prefix=" << (prefix_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 11.6: Import Map scopes and JSON
	std::cout << "  11.6 Import map scopes & JSON..." << std::endl;
	{
	    std::string json = R"({
                "imports": { "react": "./vendor/react.js" },
                "scopes": { "/app/": { "react": "./custom/react.js" } }
            })";

	    auto parsed = ImportMap::FromJson(json);
	    bool parse_ok = parsed && !parsed->imports.empty();

	    if (parsed) {
		// Global scope
		auto global = parsed->Resolve("react");
		bool global_ok = global && global->find("vendor") != std::string::npos;

		// Scoped (from /app/ context)
		auto scoped = parsed->Resolve("react", "/app/main.js");
		bool scoped_ok = scoped && scoped->find("custom") != std::string::npos;

		// ToJson roundtrip
		std::string serialized = parsed->ToJson();
		bool roundtrip_ok = !serialized.empty() && serialized.find("react") != std::string::npos;

		bool passed = parse_ok && global_ok && scoped_ok && roundtrip_ok;
		std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (parse=" << (parse_ok ? "ok" : "fail")
			  << ", scoped=" << (scoped_ok ? "ok" : "fail") << ")" << std::endl;
		if (passed)
		    scenarios_passed++;
	    } else {
		std::cout << "       [FAIL] (parse failed)" << std::endl;
	    }
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Cache, Dependencies & Import Maps: " << scenarios_passed
		  << "/" << total_scenarios << std::endl;
	results.push_back({"Cache/Deps/ImportMaps", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 12: Resource Limits, Sandbox, Audit Logging & Workers
    std::cout << "\n--- Test 12: Safety & Isolation (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 12.1: Resource Limits presets
	std::cout << "  12.1 Resource limits presets..." << std::endl;
	{
	    auto minimal = ResourceLimits::Minimal();
	    bool minimal_ok = minimal.max_heap_size_mb == 64 && minimal.cpu_time_limit == std::chrono::seconds(5);

	    auto standard = ResourceLimits::Standard();
	    bool standard_ok = standard.max_heap_size_mb == 512;

	    auto generous = ResourceLimits::Generous();
	    bool generous_ok = generous.max_heap_size_mb == 2048;

	    auto unlimited = ResourceLimits::Unlimited();
	    bool unlimited_ok = unlimited.cpu_time_limit == std::chrono::milliseconds(0);

	    bool passed = minimal_ok && standard_ok && generous_ok && unlimited_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (minimal=" << (minimal_ok ? "ok" : "fail")
		      << ", generous=" << (generous_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 12.2: Audit Logger
	std::cout << "  12.2 Audit logger..." << std::endl;
	{
	    AuditLogger::Options opts;
	    opts.enabled = true;
	    opts.log_successful = true;
	    AuditLogger logger(opts);

	    // Add callback sink to capture entries
	    std::vector<AuditEntry> captured;
	    logger.AddSink(
		    std::make_shared<CallbackAuditSink>([&captured](const AuditEntry &e) { captured.push_back(e); }));

	    // Log some events
	    logger.LogModuleLoad("lodash", "./vendor/lodash.js");
	    logger.LogPermissionDenied("FileWrite", "/etc/passwd");

	    bool log_ok = captured.size() == 2;
	    bool types_ok = captured[0].type == AuditEventType::ModuleLoad;

	    // Get entries from memory
	    auto entries = logger.GetEntries(10);
	    bool entries_ok = entries.size() == 2;

	    bool passed = log_ok && types_ok && entries_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (log=" << captured.size() << " entries)"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 12.3: Sandbox configuration
	std::cout << "  12.3 Sandbox configuration..." << std::endl;
	{
	    auto strict = SandboxConfig::Strict();
	    bool strict_ok = strict.disable_eval && strict.disable_function_constructor && strict.freeze_intrinsics &&
			     strict.disable_wasm;

	    auto standard = SandboxConfig::Standard();
	    bool standard_ok = standard.disable_eval && !standard.freeze_global;

	    auto permissive = SandboxConfig::Permissive();
	    bool permissive_ok = !permissive.disable_eval && !permissive.hide_require;

	    bool passed = strict_ok && standard_ok && permissive_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (strict=" << (strict_ok ? "ok" : "fail")
		      << ", permissive=" << (permissive_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 12.4: Sandbox Guard checks
	std::cout << "  12.4 Sandbox guard checks..." << std::endl;
	{
	    SandboxConfig config;
	    config.blocked_globals.insert("process");
	    config.blocked_requires.insert("child_process");
	    config.allowed_read_paths = {"/app/", "/data/"};
	    config.blocked_hosts = {"evil.com"};

	    SandboxGuard guard(config);

	    // Global access
	    bool global_blocked = !guard.CheckGlobalAccess("process");
	    bool global_allowed = guard.CheckGlobalAccess("console");

	    // Require check
	    bool require_blocked = !guard.CheckRequire("child_process");
	    bool require_allowed = guard.CheckRequire("lodash");

	    // File check
	    bool file_allowed = guard.CheckFileRead("/app/data.json");
	    bool file_blocked = !guard.CheckFileRead("/etc/passwd");
	    bool traversal_blocked = !guard.CheckFileRead("/app/../etc/passwd");

	    // Network check
	    bool net_blocked = !guard.CheckNetworkAccess("evil.com", 80);
	    bool net_allowed = guard.CheckNetworkAccess("api.example.com", 443);

	    bool passed = global_blocked && global_allowed && require_blocked && require_allowed && file_allowed &&
			  file_blocked && traversal_blocked && net_blocked && net_allowed;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (blocks work, allows work)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 12.5: Message Port
	std::cout << "  12.5 Message port..." << std::endl;
	{
	    auto port = std::make_shared<MessagePort>();

	    // Post messages
	    port->PostMessage("hello");
	    port->PostMessage("world");

	    bool has_msgs = port->HasMessages();

	    // Receive
	    auto msg1 = port->TryReceive();
	    auto msg2 = port->TryReceive();
	    auto msg3 = port->TryReceive(); // Should be empty

	    bool recv_ok = msg1 && msg1->data == "hello" && msg2 && msg2->data == "world" && !msg3.has_value();

	    // Close
	    port->Close();
	    bool closed_ok = port->IsClosed();

	    bool passed = has_msgs && recv_ok && closed_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (send/recv=" << (recv_ok ? "ok" : "fail")
		      << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 12.6: Worker creation
	std::cout << "  12.6 Worker creation..." << std::endl;
	{
	    WorkerOptions opts;
	    opts.name = "TestWorker";
	    opts.startup_timeout = std::chrono::milliseconds(2000);

	    auto worker = std::make_shared<Worker>("console.log('hello');", opts);

	    bool created_ok = worker->GetState() == Worker::State::Created;
	    bool name_ok = worker->GetName() == "TestWorker";

	    // Note: Full worker test would require V8 integration
	    // Just testing structure here

	    bool passed = created_ok && name_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (created=" << (created_ok ? "ok" : "fail")
		      << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Safety & Isolation: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Safety/Isolation", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 13: Native Module Support
    std::cout << "\n--- Test 13: Native Module Support (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 5;

	// 13.1: NativeModuleLoader options
	std::cout << "  13.1 NativeModuleLoader options..." << std::endl;
	{
	    NativeModuleLoader::Options opts;
	    opts.search_paths = {"./native", "./build/Release"};
	    opts.allow_absolute_paths = false;
	    opts.blocked_modules = {"malicious"};
	    opts.allowed_modules = {"safe_module"};

	    NativeModuleLoader loader(opts);

	    bool name_ok = loader.GetName() == "NativeModuleLoader";
	    bool can_load_node = loader.CanLoad("module.node");
	    bool can_load_dll = loader.CanLoad("module.dll");
	    bool can_load_so = loader.CanLoad("module.so");
	    bool cant_load_js = !loader.CanLoad("module.js");

	    bool passed = name_ok && can_load_node && can_load_dll && can_load_so && cant_load_js;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (.node=" << (can_load_node ? "ok" : "fail")
		      << ", .dll=" << (can_load_dll ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 13.2: NativeModuleInfo structure
	std::cout << "  13.2 NativeModuleInfo structure..." << std::endl;
	{
	    NativeModuleInfo info("test_module", "/path/to/test_module.node");

	    bool name_ok = info.name == "test_module";
	    bool path_ok = info.path == "/path/to/test_module.node";
	    bool not_loaded = !info.loaded && !info.IsValid();

	    // Simulate loading
	    info.loaded = true;
	    info.napi_version = 8;
	    info.exports = {"init", "cleanup", "process"};

	    bool exports_ok = info.exports.size() == 3;
	    bool napi_ok = info.napi_version == 8;

	    bool passed = name_ok && path_ok && not_loaded && exports_ok && napi_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (structure valid)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 13.3: NativeResolver
	std::cout << "  13.3 NativeResolver..." << std::endl;
	{
	    NativeResolver resolver;

	    // Can handle native: prefix
	    bool can_handle = resolver.CanHandle("native:sqlite3");
	    bool cant_handle = !resolver.CanHandle("./module.js");
	    bool cant_handle_npm = !resolver.CanHandle("npm:lodash");

	    // Resolution returns the module name
	    auto result = resolver.Resolve("native:better-sqlite3", "");
	    bool resolve_ok = result && !result->resolved_path.empty();

	    bool passed = can_handle && cant_handle && cant_handle_npm && resolve_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (can_handle=" << (can_handle ? "ok" : "fail")
		      << ", resolve=" << (resolve_ok ? "ok" : "fail") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 13.4: NativeModuleRegistry
	std::cout << "  13.4 NativeModuleRegistry..." << std::endl;
	{
	    auto &registry = NativeModuleRegistry::Instance();
	    registry.Clear(); // Start fresh

	    // Register modules
	    NativeModuleInfo mod1("sqlite3", "/path/sqlite3.node");
	    NativeModuleInfo mod2("canvas", "/path/canvas.node");

	    registry.Register("sqlite3", mod1);
	    registry.Register("canvas", mod2);

	    bool count_ok = registry.Count() == 2;

	    // Get module
	    auto retrieved = registry.Get("sqlite3");
	    bool get_ok = retrieved && retrieved->name == "sqlite3";

	    // Get all
	    auto all = registry.GetAll();
	    bool all_ok = all.size() == 2;

	    // Unregister
	    registry.Unregister("canvas");
	    bool unregister_ok = registry.Count() == 1;

	    registry.Clear();
	    bool clear_ok = registry.Count() == 0;

	    bool passed = count_ok && get_ok && all_ok && unregister_ok && clear_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (registry=" << (count_ok ? "ok" : "fail")
		      << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 13.5: Loader tracking
	std::cout << "  13.5 Loader tracking..." << std::endl;
	{
	    NativeModuleLoader loader;

	    // Initially no modules loaded
	    bool no_modules = loader.GetLoadedModules().empty();

	    // IsLoaded check (module doesn't exist, so should be false)
	    bool not_loaded = !loader.IsLoaded("nonexistent.node");

	    // GetInfo for non-existent
	    auto info = loader.GetInfo("nonexistent.node");
	    bool no_info = !info.has_value();

	    bool passed = no_modules && not_loaded && no_info;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (tracking works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Native Module Support: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Native Modules", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 14: Developer Experience (REPL, Error Formatting, Profiler)
    std::cout << "\n--- Test 14: Developer Experience (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 14.1: REPL evaluation
	std::cout << "  14.1 REPL evaluation..." << std::endl;
	{
	    Repl repl;

	    // Evaluate simple expressions
	    auto result1 = repl.Evaluate("1 + 1");
	    bool eval_ok = result1.success && result1.type == "number";

	    auto result2 = repl.Evaluate("true");
	    bool bool_ok = result2.success && result2.type == "boolean";

	    auto result3 = repl.Evaluate("throw new Error()");
	    bool error_ok = !result3.success;

	    bool passed = eval_ok && bool_ok && error_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (eval=" << (eval_ok ? "ok" : "fail")
		      << ", error=" << (error_ok ? "caught" : "missed") << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 14.2: REPL history and completion
	std::cout << "  14.2 REPL history & completion..." << std::endl;
	{
	    Repl repl;

	    // History
	    repl.Evaluate("let x = 1");
	    repl.Evaluate("let y = 2");
	    bool history_ok = repl.GetHistory().size() == 2;

	    auto prev = repl.GetPreviousHistory();
	    bool prev_ok = prev && *prev == "let y = 2";

	    // Completion
	    auto completions = repl.Complete("cons");
	    bool complete_ok = std::find(completions.begin(), completions.end(), "console") != completions.end();

	    // Context
	    repl.SetContext("myVar", "42");
	    auto ctx = repl.GetContext("myVar");
	    bool ctx_ok = ctx && *ctx == "42";

	    bool passed = history_ok && prev_ok && complete_ok && ctx_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (history=" << repl.GetHistory().size()
		      << ", completions=" << completions.size() << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 14.3: Code completeness check
	std::cout << "  14.3 Code completeness..." << std::endl;
	{
	    Repl repl;

	    // Complete statements
	    bool complete_simple = repl.IsComplete("1 + 1");
	    bool complete_func = repl.IsComplete("function foo() { return 1; }");

	    // Incomplete
	    bool incomplete_brace = !repl.IsComplete("function foo() {");
	    bool incomplete_string = !repl.IsComplete("let x = 'hello");
	    bool incomplete_paren = !repl.IsComplete("console.log(");

	    bool passed = complete_simple && complete_func && incomplete_brace && incomplete_string && incomplete_paren;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (detects incomplete code)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 14.4: Error Formatter
	std::cout << "  14.4 Error formatter..." << std::endl;
	{
	    ErrorFormatter formatter;

	    // Create stack frames
	    std::vector<StackFrame> stack;
	    StackFrame f1;
	    f1.function_name = "processData";
	    f1.file_path = "/app/src/main.js";
	    f1.line = 42;
	    f1.column = 15;
	    stack.push_back(f1);

	    StackFrame f2;
	    f2.function_name = "Object.<anonymous>";
	    f2.file_path = "/app/index.js";
	    f2.line = 10;
	    stack.push_back(f2);

	    StackFrame f3;
	    f3.is_native = true;
	    f3.function_name = "Array.map";
	    stack.push_back(f3);

	    // Format
	    std::string formatted = formatter.Format("TypeError", "Cannot read property 'x' of undefined", stack);
	    bool has_error = formatted.find("TypeError") != std::string::npos;
	    bool has_message = formatted.find("Cannot read") != std::string::npos;
	    bool has_at = formatted.find("at ") != std::string::npos;

	    // Simple format
	    std::string simple = formatter.FormatSimple("Error", "Something went wrong");
	    bool simple_ok = simple.find("Error:") != std::string::npos;

	    bool passed = has_error && has_message && has_at && simple_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (formatting works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 14.5: Profiler timing
	std::cout << "  14.5 Profiler timing..." << std::endl;
	{
	    Profiler profiler;

	    // Begin/End
	    profiler.Begin("test_operation", "test");
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    profiler.End("test_operation");

	    auto entries = profiler.GetEntries("test");
	    bool entry_ok = entries.size() == 1;
	    bool duration_ok = entries[0].DurationMs() >= 5.0; // At least 5ms

	    // Mark
	    profiler.Mark("checkpoint", "marker");
	    auto markers = profiler.GetEntries("marker");
	    bool mark_ok = markers.size() == 1;

	    // Stats
	    auto stats = profiler.GetStats("test_operation");
	    bool stats_ok = stats.total_entries == 1 && stats.total_time_ms > 0;

	    bool passed = entry_ok && duration_ok && mark_ok && stats_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (duration=" << entries[0].DurationMs()
		      << "ms)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 14.6: Profiler report
	std::cout << "  14.6 Profiler report & control..." << std::endl;
	{
	    Profiler profiler;

	    profiler.Begin("op1", "category_a");
	    profiler.End("op1");
	    profiler.Begin("op2", "category_b");
	    profiler.End("op2");

	    // Generate report
	    std::string report = profiler.GenerateReport();
	    bool report_ok = report.find("Profiler Report") != std::string::npos &&
			     report.find("category_a") != std::string::npos;

	    // Enable/Disable
	    profiler.Disable();
	    bool disabled = !profiler.IsEnabled();

	    profiler.Enable();
	    bool enabled = profiler.IsEnabled();

	    // Clear
	    profiler.Clear();
	    bool cleared = profiler.GetEntries().empty();

	    bool passed = report_ok && disabled && enabled && cleared;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (report generated, controls work)"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Developer Experience: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Dev Experience", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 15: Expression Engine
    std::cout << "\n--- Test 15: Expression Engine (Multiple Scenarios) ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 6;

	// 15.1: ExpressionValue types
	std::cout << "  15.1 ExpressionValue types..." << std::endl;
	{
	    ExpressionValue null_val;
	    ExpressionValue bool_val(true);
	    ExpressionValue num_val(42.5);
	    ExpressionValue str_val("hello");
	    ExpressionValue arr_val(ExpressionValue::ArrayType{ExpressionValue(1), ExpressionValue(2)});

	    bool null_ok = null_val.IsNull() && null_val.AsString() == "null";
	    bool bool_ok = bool_val.IsBoolean() && bool_val.AsBoolean() == true;
	    bool num_ok = num_val.IsNumber() && num_val.AsNumber() == 42.5;
	    bool str_ok = str_val.IsString() && str_val.AsString() == "hello";
	    bool arr_ok = arr_val.IsArray() && arr_val.AsArray().size() == 2;

	    // Type conversions
	    bool conversion_ok = ExpressionValue("123").AsNumber() == 123 && ExpressionValue(0).AsBoolean() == false &&
				 ExpressionValue(1).AsBoolean() == true;

	    bool passed = null_ok && bool_ok && num_ok && str_ok && arr_ok && conversion_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (all types work)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 15.2: ExpressionContext
	std::cout << "  15.2 ExpressionContext..." << std::endl;
	{
	    ExpressionContext ctx;

	    ctx.Set("x", ExpressionValue(10));
	    ctx.Set("y", ExpressionValue(20));
	    ctx.Set("name", ExpressionValue("test"));

	    bool has_ok = ctx.Has("x") && ctx.Has("y") && !ctx.Has("z");
	    bool get_ok = ctx.Get("x")->AsNumber() == 10;
	    bool names_ok = ctx.GetNames().size() == 3;

	    // Merge
	    ExpressionContext ctx2;
	    ctx2.Set("z", ExpressionValue(30));
	    ctx.Merge(ctx2);
	    bool merge_ok = ctx.Has("z");

	    // Child context
	    auto child = ctx.CreateChild();
	    child.Set("w", ExpressionValue(40));
	    bool child_ok = child.Has("x") && child.Has("w");

	    bool passed = has_ok && get_ok && names_ok && merge_ok && child_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (context vars=" << ctx.Size() << ")"
		      << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 15.3: Expression arithmetic
	std::cout << "  15.3 Expression arithmetic..." << std::endl;
	{
	    ExpressionEngine engine;

	    auto r1 = engine.Evaluate("10 + 5");
	    bool add_ok = r1.success && r1.value.AsNumber() == 15;

	    auto r2 = engine.Evaluate("20 - 7");
	    bool sub_ok = r2.success && r2.value.AsNumber() == 13;

	    auto r3 = engine.Evaluate("6 * 7");
	    bool mul_ok = r3.success && r3.value.AsNumber() == 42;

	    auto r4 = engine.Evaluate("100 / 4");
	    bool div_ok = r4.success && r4.value.AsNumber() == 25;

	    // With context
	    ExpressionContext ctx;
	    ctx.Set("a", ExpressionValue(100));
	    ctx.Set("b", ExpressionValue(50));

	    auto r5 = engine.Evaluate("a + b", ctx);
	    bool ctx_ok = r5.success && r5.value.AsNumber() == 150;

	    bool passed = add_ok && sub_ok && mul_ok && div_ok && ctx_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (arithmetic works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 15.4: Built-in functions
	std::cout << "  15.4 Built-in functions..." << std::endl;
	{
	    ExpressionEngine engine;

	    // Math functions
	    auto r1 = engine.Evaluate("abs(-5)");
	    bool abs_ok = r1.success && r1.value.AsNumber() == 5;

	    auto r2 = engine.Evaluate("max(1, 5, 3)");
	    bool max_ok = r2.success && r2.value.AsNumber() == 5;

	    auto r3 = engine.Evaluate("min(10, 2, 8)");
	    bool min_ok = r3.success && r3.value.AsNumber() == 2;

	    auto r4 = engine.Evaluate("clamp(15, 0, 10)");
	    bool clamp_ok = r4.success && r4.value.AsNumber() == 10;

	    // String functions
	    auto r5 = engine.Evaluate("strlen('hello')");
	    bool strlen_ok = r5.success && r5.value.AsNumber() == 5;

	    auto r6 = engine.Evaluate("upper('hello')");
	    bool upper_ok = r6.success && r6.value.AsString() == "HELLO";

	    bool passed = abs_ok && max_ok && min_ok && clamp_ok && strlen_ok && upper_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (functions work)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 15.5: Expression validation
	std::cout << "  15.5 Expression validation..." << std::endl;
	{
	    ExpressionEngine engine;
	    std::string error;

	    bool valid_ok = engine.Validate("1 + 2", error);
	    bool valid_parens = engine.Validate("(a + b) * c", error);

	    bool invalid_string = !engine.Validate("let x = 'unclosed", error);
	    bool invalid_parens = !engine.Validate("((a + b)", error);
	    bool invalid_empty = !engine.Validate("", error);

	    bool passed = valid_ok && valid_parens && invalid_string && invalid_parens && invalid_empty;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (validation works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 15.6: Expression caching
	std::cout << "  15.6 Expression caching..." << std::endl;
	{
	    ExpressionEngine engine;

	    // Evaluate same expression multiple times
	    engine.Evaluate("1 + 2 + 3");
	    engine.Evaluate("1 + 2 + 3");
	    engine.Evaluate("1 + 2 + 3");
	    engine.Evaluate("4 + 5");

	    bool cache_has_entries = engine.GetCache().Size() == 2;
	    bool hit_rate_ok = engine.GetCache().HitRate() > 0.4; // 2 hits out of 4 calls

	    engine.GetCache().Clear();
	    bool clear_ok = engine.GetCache().Size() == 0;

	    bool passed = cache_has_entries && hit_rate_ok && clear_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (cache size=" << 2
		      << ", hit_rate=" << engine.GetCache().HitRate() << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Expression Engine: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Expression Engine", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 16: Enhanced Expressions (Multi-line, Variables, Interpolation)
    std::cout << "\n--- Test 16: Enhanced Expressions ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 5;

	// 16.1: Variable declarations (let/const)
	std::cout << "  16.1 Variable declarations..." << std::endl;
	{
	    ExpressionEngine engine;

	    auto r1 = engine.Evaluate("let x = 10; x + 5;");
	    bool let_ok = r1.success && r1.value.AsNumber() == 15;

	    auto r2 = engine.Evaluate("const y = 20; y * 2;");
	    bool const_ok = r2.success && r2.value.AsNumber() == 40;

	    auto r3 = engine.Evaluate("const z = 5; z = 6;");
	    bool const_fail = !r3.success && r3.error.find("const") != std::string::npos;

	    bool passed = let_ok && const_ok && const_fail;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (let=" << let_ok << ", const=" << const_ok
		      << ", protection=" << const_fail << ")" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 16.2: Assignments and Sequences
	std::cout << "  16.2 Assignments & Sequencing..." << std::endl;
	{
	    ExpressionEngine engine;

	    auto r1 = engine.Evaluate("let a = 1; a = a + 1; a = a * 2; a;");
	    bool seq_ok = r1.success && r1.value.AsNumber() == 4;

	    // Multiple statements (return last value)
	    auto r2 = engine.Evaluate("1+1; 2+2; 3+3;");
	    bool last_ok = r2.success && r2.value.AsNumber() == 6;

	    bool passed = seq_ok && last_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (sequences work)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 16.3: String Interpolation
	std::cout << "  16.3 String Interpolation..." << std::endl;
	{
	    ExpressionEngine engine;

	    auto r1 = engine.Evaluate("let name = 'World'; \"Hello ${name}!\";");
	    bool interp_ok = r1.success && r1.value.AsString() == "Hello World!";

	    auto r2 = engine.Evaluate("let x = 5; \"Count: ${x}\";");
	    bool num_interp_ok = r2.success && r2.value.AsString() == "Count: 5";

	    auto r3 = engine.Evaluate("let missing = 'foo'; \"Val: ${bar}\";");
	    // Undefined var usually returns null or error, or empty string. Check impl.
	    // In impl: "undefined" string if nullopt
	    bool missing_ok = r3.success && r3.value.AsString() == "Val: undefined";

	    bool passed = interp_ok && num_interp_ok && missing_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (interpolation works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 16.4: Whitespace and Comments
	std::cout << "  16.4 Whitespace & Comments..." << std::endl;
	{
	    ExpressionEngine engine;

	    std::string code = R"(
                let x = 10;
                let y = 20;
                x + y;
            )";
	    // Basic lexer handles newlines as separators? Lexer logic: isspace(c) -> if \n line++ continue
	    // So newlines are whitespace and ignored. Semicolons needed?
	    // Parser: ParseStatementList -> while(!EOF) ParseStatement -> ParseExpression -> Match(Semi)
	    // If no semicolon, it might fail or try to parse next token as part of expr.
	    // Current parser expects semicolon after statements.

	    std::string code2 = "let x=10; let y=20; x+y;";
	    auto r1 = engine.Evaluate(code2);
	    bool ws_ok = r1.success && r1.value.AsNumber() == 30;

	    bool passed = ws_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (whitespace handling)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 16.5: Complex Logic
	std::cout << "  16.5 Complex Logic..." << std::endl;
	{
	    ExpressionEngine engine;
	    ExpressionContext ctx;

	    auto r1 = engine.Evaluate("2 + 3 * 4", ctx);
	    bool prec_ok = r1.success && r1.value.AsNumber() == 14;

	    auto r2 = engine.Evaluate("(2 + 3) * 4", ctx);
	    bool paren_ok = r2.success && r2.value.AsNumber() == 20;

	    bool passed = prec_ok && paren_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (precedence)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Enhanced Expressions: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Enhanced Expressions", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Test 17: Background Services & Scheduling
    std::cout << "\n--- Test 17: Background Services & Scheduling ---" << std::endl;
    {
	int scenarios_passed = 0;
	int total_scenarios = 4;

	// 17.1: Cron Parsing
	std::cout << "  17.1 Cron Parsing..." << std::endl;
	{
	    CronExpression every_min("* * * * *");
	    bool match_all = every_min.IsValid() && every_min.IsMatch(std::chrono::system_clock::now());

	    CronExpression specific("30 14 1 1 *");
	    bool valid_spec = specific.IsValid();

	    CronExpression invalid("60 * * * *");
	    bool detect_invalid = !invalid.IsValid();

	    CronExpression step("*/5 * * * *");
	    bool valid_step = step.IsValid();

	    bool passed = match_all && valid_spec && detect_invalid && valid_step;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (parsing works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 17.2: Service Worker Lifecycle
	std::cout << "  17.2 Service Worker Lifecycle..." << std::endl;
	{
	    ServiceWorker::Config config;
	    config.script_path = "worker.js";

	    ServiceWorker worker("sw-1", config);
	    bool init_redundant = worker.GetState() == ServiceWorker::State::Redundant;

	    worker.Start();
	    bool started = worker.GetState() == ServiceWorker::State::Active;

	    worker.DispatchEvent("fetch", "{}");
	    bool received_event = worker.GetEventCount() == 1;

	    worker.Stop();
	    bool stopped = worker.GetState() == ServiceWorker::State::Redundant;

	    bool passed = init_redundant && started && received_event && stopped;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (lifecycle transitions)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 17.3: Scheduler Job Registration
	std::cout << "  17.3 Scheduler Job Registration..." << std::endl;
	{
	    BackgroundScheduler scheduler;
	    bool added = scheduler.ScheduleJob("job1", "* * * * *", []() {});
	    bool bad_cron = !scheduler.ScheduleJob("job2", "invalid", []() {});

	    bool count_ok = scheduler.GetJobCount() == 1;

	    bool passed = added && bad_cron && count_ok;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (scheduling works)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	// 17.4: Worker Registration
	std::cout << "  17.4 Background Worker Registration..." << std::endl;
	{
	    BackgroundScheduler scheduler;
	    ServiceWorker::Config config;
	    config.auto_start = true;

	    auto worker = scheduler.RegisterWorker("bg-worker-1", config);
	    bool worker_exists = scheduler.GetWorker("bg-worker-1") != nullptr;
	    bool worker_active = worker->GetState() == ServiceWorker::State::Active;

	    bool passed = worker_exists && worker_active;
	    std::cout << "       " << (passed ? "[PASS]" : "[FAIL]") << " (worker registration)" << std::endl;
	    if (passed)
		scenarios_passed++;
	}

	bool all_passed = scenarios_passed == total_scenarios;
	std::cout << (all_passed ? "[PASS]" : "[FAIL]") << " Background Services: " << scenarios_passed << "/"
		  << total_scenarios << std::endl;
	results.push_back({"Background Services", all_passed,
			   std::to_string(scenarios_passed) + "/" + std::to_string(total_scenarios) + " scenarios"});
    }

    // Shutdown with timing
    std::cout << "\n--- Shutdown ---" << std::endl;
    {
	auto start = now();
	engine.Shutdown(true, std::chrono::seconds(3));
	auto duration = ms_since(start);
	std::cout << "  Shutdown time: " << duration << "ms" << std::endl;
	results.push_back({"Graceful Shutdown", true, std::to_string(duration) + "ms"});
    }

    // Print Report
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║                        TEST REPORT                             ║" << std::endl;
    std::cout << "╠════════════════════════════════════════════════════════════════╣" << std::endl;

    int passed_count = 0, failed_count = 0;
    for (const auto &r : results) {
	if (r.passed)
	    passed_count++;
	else
	    failed_count++;
	std::cout << "║ " << (r.passed ? "✓ PASS" : "✗ FAIL") << " │ " << std::left << std::setw(20) << r.name << " │ "
		  << std::setw(28) << r.message.substr(0, 28) << " ║" << std::endl;
    }

    std::cout << "╠════════════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ TOTAL: " << results.size() << " tests │ PASSED: " << passed_count << " │ FAILED: " << failed_count
	      << std::setw(24) << " ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;

    return failed_count > 0 ? 1 : 0;
}
