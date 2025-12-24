/**
 * test_experiments.cpp - Catch2 Test Suite for LibNode Experiments
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <unordered_set>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_approx.hpp>

// all our experimentation sources
#include "experiments/all.hpp"

using namespace experiments;
using namespace std::chrono_literals;

namespace chrono = std::chrono;

//=============================================================================
// Global Initialization
//=============================================================================

// Helper for timing
auto now = [] {
    return chrono::steady_clock::now();
};

auto ms_since = [](auto start) {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start
    ).count();
};

//=============================================================================
// Test Suites
//=============================================================================

TEST_CASE("Test 1: Basic Execution", "[script][exec]") {
    auto& engine = ScriptEngine::Instance();
    auto result = engine.ExecuteSync("40 + 2;");
    
    REQUIRE(result.IsOk());
    CHECK(result.ToString() == "42");
}

TEST_CASE("Test 2: Script Timeout", "[script][timeout]") {
    auto& engine = ScriptEngine::Instance();
    auto mainEnv = engine.GetMainEnvironment();
    REQUIRE(mainEnv->IsRunning());

    constexpr int64_t TIMEOUT_MS = 500;
    auto script = engine.CreateScript(
        "let x = 0; while(true) { x++; }",
        {
            .name = "timeout-test",
            .timeout = chrono::milliseconds(TIMEOUT_MS),
        }
    );

    CHECK(script->GetState() == ScriptState::Pending);

    auto start = now();
    bool executed = mainEnv->Execute(script);
    CHECK(executed);

    // Wait for timeout
    script->Wait(10s);
    auto duration = ms_since(start);

    CHECK(script->GetState() == ScriptState::TimedOut);
    CHECK(duration >= TIMEOUT_MS);
    CHECK(duration < (TIMEOUT_MS + 200));
}

TEST_CASE("Test 3: Priority Queue", "[script][priority]") {
    auto& engine = ScriptEngine::Instance();

    std::vector<std::string> execution_order;
    std::mutex order_mutex;

    // CRITICAL
    auto emergency = engine.CreateScript(R"(
        globalThis.emergencyResult = 'EMERGENCY_HANDLED';
        'emergency_done';
    )", {.name = "emergency", .priority = ScriptPriority::Critical});

    // HIGH
    auto userAction = engine.CreateScript(R"(
        const result = { action: 'click' };
        JSON.stringify(result);
    )", {.name = "userAction", .priority = ScriptPriority::High});

    // NORMAL
    auto dataProcess = engine.CreateScript(R"(
        let sum = 0; for (let i = 0; i < 1000; i++) sum += i;
        'data_processed:' + sum;
    )", {.name = "dataProcess", .priority = ScriptPriority::Normal});

    // LOW
    auto cleanup1 = engine.CreateScript("'cleanup1';", {.name = "cleanup1", .priority = ScriptPriority::Low});
    auto cleanup2 = engine.CreateScript("'cleanup2';", {.name = "cleanup2", .priority = ScriptPriority::Low});

    auto trackOrder = [&](const std::string& name) {
        return [&, name](bool success, const std::string&, const ScriptError&) {
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

    // Queue in REVERSE priority order
    engine.GetMainEnvironment()->Execute(cleanup1);
    engine.GetMainEnvironment()->Execute(cleanup2);
    engine.GetMainEnvironment()->Execute(dataProcess);
    engine.GetMainEnvironment()->Execute(userAction);
    engine.GetMainEnvironment()->Execute(emergency);

    // Small delay to ensure all scripts are in the priority queue before processing
    std::this_thread::sleep_for(50ms);

    // Wait for all
    emergency->Wait(5s);
    userAction->Wait(5s);
    dataProcess->Wait(5s);
    cleanup1->Wait(5s);
    cleanup2->Wait(5s);

    CHECK(emergency->GetState() == ScriptState::Completed);
    CHECK(userAction->GetState() == ScriptState::Completed);
    CHECK(dataProcess->GetState() == ScriptState::Completed);
    CHECK(cleanup1->GetState() == ScriptState::Completed);
    CHECK(cleanup2->GetState() == ScriptState::Completed);

    REQUIRE(!execution_order.empty());
    CHECK(execution_order[0] == "emergency");
}

TEST_CASE("Test 4: Script Context", "[script][context]") {
    SECTION("4.1 Basic CRUD") {
        auto ctx = std::make_shared<ScriptContext>();

        // Create
        ctx->Set("user", "john");
        ctx->Set("age", "30");

        // Read
        CHECK(ctx->Get("user").value_or("") == "john");
        CHECK(ctx->Get("age").value_or("") == "30");

        // Update
        ctx->Set("age", "31");
        CHECK(ctx->Get("age").value_or("") == "31");

        // Delete
        ctx->Remove("user");
        CHECK(!ctx->Get("user").has_value());

	// Non-existent
        CHECK(!ctx->Get("nonexistent").has_value());
    }

    SECTION("4.2 Concurrent Access") {
        auto ctx = std::make_shared<ScriptContext>();
        std::atomic success_count{0};
        std::vector<std::thread> threads;

        // Multiple writers
        for (int i = 0; i < 10; i++) {
            threads.emplace_back([ctx, i, &success_count] {
		const std::string key = "thread_" + std::to_string(i);
                ctx->Set(key, std::to_string(i * 10));
                std::this_thread::sleep_for(5ms);
                if (ctx->Get(key).has_value()) {
                    ++success_count;
                }
            });
        }

        // Multiple readers
        for (int i = 0; i < 5; i++) {
            threads.emplace_back([ctx] {
                for (int j = 0; j < 10; j++) {
                    ctx->Get("thread_" + std::to_string(j));
                }
            });
        }

        for (auto& t : threads) t.join();

        CHECK(success_count == 10);
    }

    SECTION("4.3 Bulk Operations") {
        auto ctx = std::make_shared<ScriptContext>();

        // Add 100 items
        for (int i = 0; i < 100; i++) {
            ctx->Set("item_" + std::to_string(i), "value_" + std::to_string(i));
        }

        // Verify random samples
        CHECK(ctx->Get("item_0").value_or("") == "value_0");
        CHECK(ctx->Get("item_50").value_or("") == "value_50");
        CHECK(ctx->Get("item_99").value_or("") == "value_99");

        // Get all and verify count
        auto all = ctx->GetAll();
        CHECK(all.size() == 100);

        // Clear and verify empty
        ctx->Clear();
        CHECK(ctx->GetAll().empty());
    }

    SECTION("4.4 Config Pattern") {
        auto config = std::make_shared<ScriptContext>();

        // Set default config
        config->Set("log_level", "info");
        config->Set("max_connections", "100");
        config->Set("timeout_ms", "5000");
        config->Set("feature_flag_x", "true");

        // Read with defaults
        auto getConfig = [&](const std::string& key, const std::string& def) {
            return config->Get(key).value_or(def);
        };

        CHECK(getConfig("log_level", "warn") == "info");
        CHECK(getConfig("max_connections", "50") == "100");
        CHECK(getConfig("missing_key", "default") == "default");
    }

    SECTION("4.5 Script Data Sharing") {
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

        CHECK(workflow_complete);
        CHECK(shared->Get("script2_used").value_or("") == "processed_data");
    }
}

TEST_CASE("Test 5: Metrics", "[script][metrics]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();

    // Ensure some activity and memory usage
    env->ExecuteSync("const buffer = new Array(1000).fill(0);");

    // Metrics are updated periodically in the background thread (every ~10 loop ticks)
    // We wait a bit to ensure the update cycle runs at least once after our script
    std::this_thread::sleep_for(200ms);

    auto metrics = env->GetMetrics();

    CHECK(metrics.execution.scripts_executed > 0);
    CHECK(metrics.memory.used_heap_size > 0);
    CHECK(metrics.memory.total_heap_size > 0);
}

TEST_CASE("Test 6: Custom Environment", "[script][environment]") {
    auto& engine = ScriptEngine::Instance();

    SECTION("6.1 Bootstrap Script") {
        EnvironmentConfig config;
        config.name = "Bootstrap-Test";
        config.bootstrap_script = "globalThis.API_VERSION = '2.0'; globalThis.DEBUG = true;";

        auto env = engine.CreateEnvironment(config);

        REQUIRE(env);

        CHECK(env->ExecuteSync("API_VERSION;", 2s).ToString() == "2.0");
        CHECK(env->ExecuteSync("DEBUG;", 2s).ToString() == "true");
    }

    SECTION("6.2 Isolation") {
        EnvironmentConfig c1, c2;
        c1.name = "Iso1"; c1.bootstrap_script = "globalThis.id='1';";
        c2.name = "Iso2"; c2.bootstrap_script = "globalThis.id='2';";

        auto env1 = engine.CreateEnvironment(c1);
        auto env2 = engine.CreateEnvironment(c2);

        CHECK(env1->ExecuteSync("id;", 1s).ToString() == "1");
        CHECK(env2->ExecuteSync("id;", 1s).ToString() == "2");
    }

    SECTION("6.3 Multi-script Execution") {
        EnvironmentConfig config;
        config.name = "Multi-Script";
        config.bootstrap_script = "globalThis.cnt = 0;";

        auto env = engine.CreateEnvironment(config);

        env->ExecuteSync("cnt++;", 1s);
        env->ExecuteSync("cnt++;", 1s);
        env->ExecuteSync("cnt++;", 1s);

        CHECK(env->ExecuteSync("cnt;", 1s).ToString() == "3");
    }

    SECTION("6.4 Default Timeout") {
        EnvironmentConfig config;
        config.name = "Timeout-Config";
        config.default_script_timeout = 100ms;

        auto env = engine.CreateEnvironment(config);

        auto script = std::make_shared<Script>("while(true){}", Script::Options{});
        env->Execute(script);
        script->Wait(2s);

        CHECK(script->GetState() == ScriptState::TimedOut);
    }
}

TEST_CASE("Test 7: Error Handling", "[script][error]") {
    auto& engine = ScriptEngine::Instance();

    SECTION("7.1 Runtime Error") {
        auto res = engine.ExecuteSync("throw new Error('fail');");
        CHECK(res.IsError());
        CHECK(res.Error().code == ErrorCode::RuntimeError);
        CHECK(res.Error().message.find("fail") != std::string::npos);
    }

    SECTION("7.2 Syntax Error") {
        auto res = engine.ExecuteSync("function { bad syntax");
        CHECK(res.IsError());
        CHECK(res.Error().code == ErrorCode::CompileError);
    }

    SECTION("7.3 Reference Error") {
        auto res = engine.ExecuteSync("undefinedVar.prop;");
        CHECK(res.IsError());
        CHECK(res.Error().code == ErrorCode::RuntimeError);
    }

    SECTION("7.4 Type Error") {
        auto res = engine.ExecuteSync("null.toString();");
        CHECK(res.IsError());
        CHECK(res.Error().code == ErrorCode::RuntimeError);
    }

    SECTION("7.5 Stack Trace") {
        auto res = engine.ExecuteSync(R"(
            function level3() { throw new Error('deep error'); }
            function level2() { level3(); }
            function level1() { level2(); }
            level1();
        )", 2s);

        CHECK(res.IsError());
        CHECK(!res.Error().stack.empty());
        CHECK(res.Error().stack.find("level") != std::string::npos);
    }
}

TEST_CASE("Test 8: Module System", "[script][modules]") {
    SECTION("8.1 Virtual Resolver/Loader") {
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
        REQUIRE(resolved1.has_value());
        auto resolved2 = resolver->Resolve("virtual:config", "");
        REQUIRE(resolved2.has_value());
        auto resolved3 = resolver->Resolve("nonexistent", "");
        REQUIRE(!resolved3.has_value());

        // Test loading
	auto loaded = loader->Load(resolved1->resolved_path);
        CHECK(loaded.has_value());
        CHECK(loaded->source.find("module.exports") != std::string::npos);

        // Test list
        auto list = loader->ListModules();
        CHECK(list.size() == 2);

        // Test unregister
        resolver->Unregister("config");
	loader->Unregister("config");
        CHECK(!resolver->Resolve("config", "").has_value());
    }

    SECTION("8.2 Disk Resolver") {
        auto resolver = std::make_shared<DiskResolver>(std::vector<std::string>{
            ".",
            "./node_modules"
        });

        CHECK(resolver->CanHandle("./test.js"));
        CHECK(resolver->CanHandle("/some/path.js"));
        CHECK(resolver->CanHandle("lodash"));
        CHECK(!resolver->CanHandle("https://example.com/module.js"));
        CHECK(!resolver->CanHandle("virtual:test"));
    }

    SECTION("8.3 Resolver Chain") {
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
        CHECK(names.size() == 2);
        CHECK(names[0] == "VirtualResolver");
        CHECK(names[1] == "DiskResolver");

        // Virtual should resolve first
        auto resolved = chain->Resolve("my-module", "");
        CHECK(resolved.has_value());
        CHECK(resolved->resolved_path.starts_with("virtual:"));

        // Can handle check
        CHECK(chain->CanHandle("my-module"));
        CHECK(chain->CanHandle("./local.js"));
        CHECK(!chain->CanHandle("https://remote.com/mod.js"));
    }

    SECTION("8.4 Integration") {
        auto r = std::make_shared<VirtualResolver>();
        auto l = std::make_shared<VirtualLoader>();

        r->Register("test-module");
        l->Register("test-module", "module.exports = 42;");

        ModuleSystem sys(r, l);

	// Test Require (resolve + load)
        auto info = sys.Require("test-module");
        REQUIRE(info.has_value());
        CHECK(info->source.find("42") != std::string::npos);

        // Test separate resolve/load
	auto resolved = sys.Resolve("test-module");
        REQUIRE(resolved.has_value());
	auto loaded = sys.Load(resolved->resolved_path);
        REQUIRE(loaded.has_value());
    }

    SECTION("8.5 Format Detection") {
        auto loader = std::make_shared<VirtualLoader>();
        loader->Register("esm", "export default 42;", ModuleFormat::ESModule);
        loader->Register("cjs", "module.exports = 42;", ModuleFormat::CommonJS);

        CHECK(loader->Load("virtual:esm")->format == ModuleFormat::ESModule);
        CHECK(loader->Load("virtual:cjs")->format == ModuleFormat::CommonJS);
    }

    SECTION("8.6 Factory Methods") {
	// Standard system
        auto standard = ModuleSystemFactory::CreateStandard();
        CHECK(standard->GetResolver() != nullptr);
        CHECK(standard->GetLoader() != nullptr);

        // Full system (with remote)
        auto full = ModuleSystemFactory::CreateFull({"https://cdn.example.com/"});
        CHECK(full->GetResolver() != nullptr);
        CHECK(full->GetLoader() != nullptr);

        // Sandboxed (virtual only)
        auto sandboxed = ModuleSystemFactory::CreateSandboxed();
        CHECK(sandboxed->GetResolver() != nullptr);
        CHECK(sandboxed->GetLoader() != nullptr);

        // Disk only
        auto disk = ModuleSystemFactory::CreateDiskOnly({
            "./lib",
            "./vendor"
        });
        CHECK(disk->GetResolver() != nullptr);
        CHECK(disk->GetLoader() != nullptr);
    }
}

TEST_CASE("Test 9: Permissions System", "[script][safety]") {
    SECTION("9.1 Grant/Revoke") {
        PermissionSet perms;

        // Start empty
        CHECK(perms.IsEmpty());

        // Grant
        perms.Grant(ScriptPermission::FileRead);
        perms.Grant(ScriptPermission::NetHttp);
        CHECK(perms.Has(ScriptPermission::FileRead));
        CHECK(perms.Has(ScriptPermission::NetHttp));
        CHECK(!perms.Has(ScriptPermission::FileWrite));

        // Revoke
        perms.Revoke(ScriptPermission::FileRead);
        CHECK(!perms.Has(ScriptPermission::FileRead));
        CHECK(perms.Has(ScriptPermission::NetHttp)); // Still has NetHttp
    }

    SECTION("9.2 Presets") {
        auto safe = PermissionSet::Safe();
        CHECK(safe.Has(ScriptPermission::Timers));
        CHECK(!safe.Has(ScriptPermission::FileRead));

        auto standard = PermissionSet::Standard();
        CHECK(standard.Has(ScriptPermission::ModuleRequire));

        auto trusted = PermissionSet::Trusted();
        CHECK(trusted.Has(ScriptPermission::ObsScenes));
        CHECK(trusted.Has(ScriptPermission::NetHttp));

        auto full = PermissionSet::Full();
        CHECK(full.Has(ScriptPermission::Full));
        CHECK(full.Has(ScriptPermission::ProcessSpawn));
    }

    SECTION("9.3 Bitwise Ops") {
        auto combined = ScriptPermission::FileRead | ScriptPermission::FileWrite;
        PermissionSet perms(combined);
        CHECK(perms.Has(ScriptPermission::FileRead));
        CHECK(perms.Has(ScriptPermission::FileWrite));

        CHECK(perms.HasAny(ScriptPermission::FileSystem));
        CHECK(!perms.HasAny(ScriptPermission::Network));
    }

    SECTION("9.4 Merging") {
        PermissionSet a(ScriptPermission::FileRead);
        PermissionSet b(ScriptPermission::NetHttp);

        // Merge (union)
        PermissionSet merged = a;
        merged.Merge(b);
        CHECK(merged.Has(ScriptPermission::FileRead));
        CHECK(merged.Has(ScriptPermission::NetHttp));

        // Intersect
        PermissionSet full(ScriptPermission::Full);
        PermissionSet limited(ScriptPermission::FileRead | ScriptPermission::Timers);
        full.Intersect(limited);
        CHECK(full.Has(ScriptPermission::FileRead));
        CHECK(full.Has(ScriptPermission::Timers));
        CHECK(!full.Has(ScriptPermission::NetHttp));
    }

    SECTION("9.5 EnvConfig Presets") {
        auto sandboxed = EnvironmentConfig::Sandboxed();
        CHECK(sandboxed.name == "Sandboxed");
        CHECK(!sandboxed.allow_file_access);
        CHECK(sandboxed.permissions.Has(ScriptPermission::Timers));

        auto trusted = EnvironmentConfig::Trusted();
        CHECK(trusted.permissions.Has(ScriptPermission::ObsScenes));

        auto default_cfg = EnvironmentConfig::Default();
        CHECK(default_cfg.permissions.Has(ScriptPermission::ModuleRequire));
    }

    SECTION("9.6 Script-level Perms") {
        // Script with explicit permissions
        Script::Options opts;
        opts.name = "sandboxed-script";
        opts.permissions = PermissionSet::Safe();
        opts.source_file = "user-scripts/untrusted.js";
        opts.author = "unknown";
        opts.trusted = false;

        auto script = std::make_shared<Script>("'test';", opts);

        CHECK(script->HasPermission(ScriptPermission::Timers));
        CHECK(!script->HasPermission(ScriptPermission::FileRead));
        CHECK(script->GetSourceFile() == "user-scripts/untrusted.js");
        CHECK(script->GetAuthor() == "unknown");
        CHECK(!script->IsTrusted());

        // Script without permissions (uses env default)
        auto default_script = std::make_shared<Script>("'test';", Script::Options{});
        CHECK(default_script->HasPermission(ScriptPermission::FileRead)); // true = uses env default
    }
}

TEST_CASE("Test 10: Package Resolvers", "[script][modules][npm]") {
    SECTION("10.1 NPM Spec") {
        // Simple package
        auto simple = NpmPackageSpec::Parse("npm:lodash");
        REQUIRE(simple.has_value());
        CHECK(simple->name == "lodash");
        CHECK(simple->version == "latest");

        // With version
        auto versioned = NpmPackageSpec::Parse("npm:lodash@4.0.0");
        REQUIRE(versioned.has_value());
        CHECK(versioned->name == "lodash");
        CHECK(versioned->version == "4.0.0");

        // Scoped package
        auto scoped = NpmPackageSpec::Parse("npm:@types/node@20.0.0");
        REQUIRE(scoped.has_value());
        CHECK(scoped->name == "@types/node");
        CHECK(scoped->version == "20.0.0");

        // With subpath
        auto subpath = NpmPackageSpec::Parse("npm:lodash@4.17.21/cloneDeep");
        REQUIRE(subpath.has_value());
        CHECK(subpath->name == "lodash");
        CHECK(subpath->version == "4.17.21");
        CHECK(subpath->subpath == "/cloneDeep");

        // Invalid
        auto invalid = NpmPackageSpec::Parse("http://example.com");
        CHECK(!invalid.has_value());
    }

    SECTION("10.2 JSR Spec") {
        // Standard JSR package
        auto versioned = JsrPackageSpec::Parse("jsr:@std/path@1.0.0");
        REQUIRE(versioned.has_value());
        CHECK(versioned->scope == "@std");
        CHECK(versioned->name == "path");
        CHECK(versioned->version == "1.0.0");

        // With subpath
        auto subpath = JsrPackageSpec::Parse("jsr:@std/fs@0.5.0/walk");
        REQUIRE(subpath.has_value());
        CHECK(subpath->scope == "@std");
        CHECK(subpath->name == "fs");
        CHECK(subpath->version == "0.5.0");
        CHECK(subpath->subpath == "/walk");

        // No version
        auto unversioned = JsrPackageSpec::Parse("jsr:@oak/oak");
        REQUIRE(unversioned.has_value());
        CHECK(unversioned->scope == "@oak");
        CHECK(unversioned->name == "oak");
        CHECK(unversioned->version.empty());

        // Invalid
        auto invalid = JsrPackageSpec::Parse("jsr:lodash");
        CHECK(!invalid.has_value());
    }

    SECTION("10.3 NPM Resolver") {
        auto r = std::make_shared<NpmResolver>();
        CHECK(r->CanHandle("npm:lodash"));
        CHECK(r->CanHandle("npm:@types/node@20.0.0"));
        CHECK(!r->CanHandle("./local.js"));
        CHECK(!r->CanHandle("jsr:@std/path"));

        auto res = r->Resolve("npm:react@18.0.0", "");
        CHECK(res.has_value());
        CHECK(res->resolved_path.find("esm.sh") != std::string::npos);
    }

    SECTION("10.4 JSR Resolver") {
        auto r = std::make_shared<JsrResolver>();
        CHECK(r->CanHandle("jsr:@std/path"));
        CHECK(!r->CanHandle("npm:lodash"));
        CHECK(!r->CanHandle("./local.js"));

        auto res = r->Resolve("jsr:@std/path@1.0.0/mod.ts", "");
        CHECK(res.has_value());
        CHECK(res->resolved_path.find("jsr.io") != std::string::npos);
    }

    SECTION("10.5 TS Transformer") {
        TypeScriptTransformer::Options opts;
        opts.generate_source_map = true;
        auto t = std::make_shared<TypeScriptTransformer>(opts);

        CHECK(t->ShouldTransform("foo.ts"));
        CHECK(t->ShouldTransform("component.tsx"));
        CHECK(t->ShouldTransform("module.mts"));
        CHECK(!t->ShouldTransform("script.js"));

        auto res = t->Transform("const x:number=1;", "f.ts", ModuleFormat::ESModule);
        CHECK(res.is_ok());
        CHECK(res.source_map.has_value());
    }

    SECTION("10.6 Transforming Loader") {
        auto inner = std::make_shared<VirtualLoader>();
        inner->Register("app.ts", "const x:number=1;");
        inner->Register("app.js", "const x=1;");

        auto loader = std::make_shared<TransformingLoader>(inner, std::make_shared<TypeScriptTransformer>());
        CHECK(loader->Load("virtual:app.ts")->transformed);
        CHECK(!loader->Load("virtual:app.js")->transformed);

        CHECK(loader->GetName().find("VirtualLoader") != std::string::npos);
    }
}

TEST_CASE("Test 11: Cache and Deps", "[script][modules][cache]") {
    SECTION("11.1 Module Cache") {
        ModuleCache::Options opts;
        opts.max_entries = 10;
	opts.max_size_bytes = 1000;
        ModuleCache cache(opts);

        // Put and get
        ModuleInfo m;
        m.specifier = "test";
        m.resolved_path = "virtual:test";
        m.source = "module.exports = 42;";
        cache.Put("virtual:test", m);

        CHECK(cache.Has("virtual:test"));
        CHECK(cache.Get("virtual:test")->source == m.source);

        // Trigger a cache miss (Has() doesn't count misses, only Get() does)
        CHECK(!cache.Has("nonexistent"));
        CHECK(!cache.Get("nonexistent").has_value());

        // Invalidate
        cache.Invalidate("virtual:test");
        CHECK(!cache.Has("virtual:test"));

        // Stats
        auto stats = cache.GetStats();
        CHECK(stats.hits == 1);
        CHECK(stats.misses == 1);
    }

    SECTION("11.2 LRU Eviction") {
        ModuleCache::Options opts;
        opts.max_entries = 3;
        ModuleCache cache(opts);
        ModuleInfo m;

        // Add 4 modules (should evict the first one)
        for (int i = 0; i < 4; i++) {
            ModuleInfo mod;
            mod.source = "code " + std::to_string(i);
            cache.Put("mod" + std::to_string(i), mod);
        }

        // First should be evicted
        CHECK(!cache.Has("mod0")); // Evicted

        CHECK(cache.Has("mod1"));
        CHECK(cache.Has("mod2"));
        CHECK(cache.Has("mod3"));

        CHECK(cache.GetStats().evictions == 1);
    }

    SECTION("11.3 Cyclic Deps") {
        DependencyGraph g(DependencyGraph::CycleAction::Warn);

        // A -> B -> C
        g.AddDependency("A", "B");
        g.AddDependency("B", "C");

        // Would C -> A create a cycle?
        CHECK(g.WouldCreateCycle("C", "A"));

        // B -> D shouldn't create a cycle
        CHECK(!g.WouldCreateCycle("B", "D"));

        auto deps = g.GetAllDependencies("A");
        CHECK(deps.size() == 2); // B and C

        // Topological order
        auto order = g.GetTopologicalOrder();
        CHECK(order.size() == 3);
    }

    SECTION("11.4 Caching Loader") {
        auto inner = std::make_shared<VirtualLoader>();
        inner->Register("lib", "module.exports = 'lib';");

        auto cache = std::make_shared<ModuleCache>();
        auto graph = std::make_shared<DependencyGraph>();
        auto loader = std::make_shared<CachingLoader>(inner, cache, graph);

        // First load (miss)
        CHECK(loader->Load("virtual:lib").has_value()); // Miss
	// Second load (hit)
        CHECK(loader->Load("virtual:lib").has_value()); // Hit

        // Check stats
        auto stats = loader->GetCacheStats();
        CHECK(stats.hits == 1);
        CHECK(stats.misses == 1);

        // Track dependency
        loader->TrackDependency("app", "virtual:lib");
        auto g = loader->GetDependencyGraph();
        CHECK(g->Size() == 2);
    }

    SECTION("11.5 Import Map Resolution") {
        ImportMap map;
        map.imports["lodash"] = "./vendor/lodash.js";
        map.imports["lodash/"] = "./vendor/lodash/";

        // Direct mapping
        auto direct = map.Resolve("lodash");
        REQUIRE(direct.has_value());
        CHECK(*direct == "./vendor/lodash.js");

        // Prefix mapping
        auto prefix = map.Resolve("lodash/cloneDeep");
        REQUIRE(prefix.has_value());
        CHECK(*prefix == "./vendor/lodash/cloneDeep");

        // Unmapped
        auto unmapped = map.Resolve("react");
        CHECK(!unmapped.has_value());
    }

    SECTION("11.6 Import Map Scopes and JSON") {
        std::string json = R"({
            "imports": { "react": "./vendor/react.js" },
            "scopes": { "/app/": { "react": "./custom/react.js" } }
        })";

        auto parsed = ImportMap::FromJson(json);
        REQUIRE(parsed.has_value());
        REQUIRE(!parsed->imports.empty());

        // Global scope
        auto global = parsed->Resolve("react");
        REQUIRE(global.has_value());
        CHECK(global->find("vendor") != std::string::npos);

        // Scoped (from /app/ context)
        auto scoped = parsed->Resolve("react", "/app/main.js");
        REQUIRE(scoped.has_value());
        CHECK(scoped->find("custom") != std::string::npos);

        // ToJson roundtrip
        std::string serialized = parsed->ToJson();
        CHECK(!serialized.empty());
        CHECK(serialized.find("react") != std::string::npos);
    }
}

TEST_CASE("Test 12: Safety and Isolation", "[safety]") {
    SECTION("12.1 Resource Limits") {
        // Minimal preset
        auto minimal = ResourceLimits::Minimal();
        CHECK(minimal.max_heap_size_mb == 64);
        CHECK(minimal.cpu_time_limit == 5s);

        // Standard preset
        auto standard = ResourceLimits::Standard();
        CHECK(standard.max_heap_size_mb == 512);

        // Generous preset
        auto generous = ResourceLimits::Generous();
        CHECK(generous.max_heap_size_mb == 2048);

        // Unlimited preset
        auto unlimited = ResourceLimits::Unlimited();
        CHECK(unlimited.cpu_time_limit == 0ms);
    }

    SECTION("12.2 Audit Logger") {
        AuditLogger::Options opts;
        opts.enabled = true;
        opts.log_successful = true;
        AuditLogger logger(opts);

        // Add callback sink to capture entries
        std::vector<AuditEntry> captured;
        logger.AddSink(
            std::make_shared<CallbackAuditSink>([&captured](const AuditEntry& e) {
                captured.push_back(e);
            })
        );

        // Log some events
        logger.LogModuleLoad("lodash", "./vendor/lodash.js");
        logger.LogPermissionDenied("FileWrite", "/etc/passwd");

        CHECK(captured.size() == 2);
        CHECK(captured[0].type == AuditEventType::ModuleLoad);

        // Get entries from memory
        auto entries = logger.GetEntries(10);
        CHECK(entries.size() == 2);
    }

    SECTION("12.3 Sandbox Config") {
        // Strict preset
        auto strict = SandboxConfig::Strict();
        CHECK(strict.disable_eval);
        CHECK(strict.disable_function_constructor);
        CHECK(strict.freeze_intrinsics);
        CHECK(strict.disable_wasm);

        // Standard preset
        auto standard = SandboxConfig::Standard();
        CHECK(standard.disable_eval);
        CHECK(!standard.freeze_global);

        // Permissive preset
        auto permissive = SandboxConfig::Permissive();
        CHECK(!permissive.disable_eval);
        CHECK(!permissive.hide_require);
    }

    SECTION("12.4 Sandbox Guard") {
        SandboxConfig config;
        config.blocked_globals.insert("process");
        config.blocked_requires.insert("child_process");
        config.allowed_read_paths = {"/app/", "/data/"};
        config.blocked_hosts = {"evil.com"};

        SandboxGuard guard(config);

        // Global access
        CHECK(!guard.CheckGlobalAccess("process"));
        CHECK(guard.CheckGlobalAccess("console"));

        // Require check
        CHECK(!guard.CheckRequire("child_process"));
        CHECK(guard.CheckRequire("lodash"));

        // File check
        CHECK(guard.CheckFileRead("/app/data.json"));
        CHECK(!guard.CheckFileRead("/etc/passwd"));
        CHECK(!guard.CheckFileRead("/app/../etc/passwd"));

        // Network check
        CHECK(!guard.CheckNetworkAccess("evil.com", 80));
        CHECK(guard.CheckNetworkAccess("api.example.com", 443));
    }

    SECTION("12.5 Message Port") {
        auto port = std::make_shared<MessagePort>();

        // Post messages
        port->PostMessage("hello");
        port->PostMessage("world");

        CHECK(port->HasMessages());

        // Receive
        auto msg1 = port->TryReceive();
        auto msg2 = port->TryReceive();
        auto msg3 = port->TryReceive(); // Should be empty

        REQUIRE(msg1.has_value());
        CHECK(msg1->data == "hello");

        REQUIRE(msg2.has_value());
        CHECK(msg2->data == "world");

        CHECK(!msg3.has_value());

        // Close
        port->Close();
        CHECK(port->IsClosed());
    }

    SECTION("12.6 Worker") {
        WorkerOptions opts;
        opts.name = "TestWorker";
        opts.startup_timeout = 2000ms;

        auto worker = std::make_shared<Worker>("console.log('hello');", opts);

        CHECK(worker->GetState() == Worker::State::Created);
        CHECK(worker->GetName() == "TestWorker");
    }
}

TEST_CASE("Test 13: Native Modules", "[modules][native]") {
    SECTION("13.1 NativeModuleLoader Options") {
        NativeModuleLoader::Options opts;
        opts.search_paths = {"./native", "./build/Release"};
        opts.allow_absolute_paths = false;
        opts.blocked_modules = {"malicious"};
        opts.allowed_modules = {"safe_module"};

        NativeModuleLoader loader(opts);

        CHECK(loader.GetName() == "NativeModuleLoader");
        CHECK(loader.CanLoad("module.node"));
        CHECK(loader.CanLoad("module.dll"));
        CHECK(loader.CanLoad("module.so"));
        CHECK(!loader.CanLoad("module.js"));
    }

    SECTION("13.2 NativeModuleInfo Structure") {
        NativeModuleInfo info("test_module", "/path/to/test_module.node");

        CHECK(info.name == "test_module");
        CHECK(info.path == "/path/to/test_module.node");
        CHECK(!info.loaded);
        CHECK(!info.IsValid());

        // Simulate loading
        info.loaded = true;
        info.napi_version = 8;
        info.exports = {"init", "cleanup", "process"};

        CHECK(info.exports.size() == 3);
        CHECK(info.napi_version == 8);
    }

    SECTION("13.3 NativeResolver") {
        NativeResolver resolver;

        // Can handle native: prefix
        CHECK(resolver.CanHandle("native:sqlite3"));
        CHECK(!resolver.CanHandle("./module.js"));
        CHECK(!resolver.CanHandle("npm:lodash"));

        // Resolution returns the module name
        auto result = resolver.Resolve("native:better-sqlite3", "");
        REQUIRE(result.has_value());
        CHECK(!result->resolved_path.empty());
    }

    SECTION("13.4 NativeModuleRegistry") {
        auto& registry = NativeModuleRegistry::Instance();
        registry.Clear(); // Start fresh

        // Register modules
        NativeModuleInfo mod1("sqlite3", "/path/sqlite3.node");
        NativeModuleInfo mod2("canvas", "/path/canvas.node");

        registry.Register("sqlite3", mod1);
        registry.Register("canvas", mod2);

        CHECK(registry.Count() == 2);

        // Get module
        auto retrieved = registry.Get("sqlite3");
        REQUIRE(retrieved.has_value());
        CHECK(retrieved->name == "sqlite3");

        // Get all
        auto all = registry.GetAll();
        CHECK(all.size() == 2);

        // Unregister
        registry.Unregister("canvas");
        CHECK(registry.Count() == 1);

        registry.Clear();
        CHECK(registry.Count() == 0);
    }

    SECTION("13.5 Loader Tracking") {
        NativeModuleLoader loader;

        // Initially no modules loaded
        CHECK(loader.GetLoadedModules().empty());

        // IsLoaded check (module doesn't exist, so should be false)
        CHECK(!loader.IsLoaded("nonexistent.node"));

        // GetInfo for non-existent
        auto info = loader.GetInfo("nonexistent.node");
        CHECK(!info.has_value());
    }
}

TEST_CASE("Test 14: Developer Experience", "[devtools]") {
    SECTION("14.1 REPL Evaluation") {
        Repl repl;

        // Evaluate simple expressions
        auto result1 = repl.Evaluate("1 + 1");
        CHECK(result1.success);
        CHECK(result1.type == "number");

        auto result2 = repl.Evaluate("true");
        CHECK(result2.success);
        CHECK(result2.type == "boolean");

        auto result3 = repl.Evaluate("throw new Error()");
        CHECK(!result3.success);
    }

    SECTION("14.2 REPL History and Completion") {
        Repl repl;

        // History
        repl.Evaluate("let x = 1");
        repl.Evaluate("let y = 2");
        CHECK(repl.GetHistory().size() == 2);

        auto prev = repl.GetPreviousHistory();
        REQUIRE(prev.has_value());
        CHECK(*prev == "let y = 2");

        // Completion
        auto completions = repl.Complete("cons");
        CHECK(std::find(completions.begin(), completions.end(), "console") != completions.end());

        // Context
        repl.SetContext("myVar", "42");
        auto ctx = repl.GetContext("myVar");
        REQUIRE(ctx.has_value());
        CHECK(*ctx == "42");
    }

    SECTION("14.3 Code Completeness") {
        Repl repl;

        // Complete statements
        CHECK(repl.IsComplete("1 + 1"));
        CHECK(repl.IsComplete("function foo() { return 1; }"));

        // Incomplete
        CHECK(!repl.IsComplete("function foo() {"));
        CHECK(!repl.IsComplete("let x = 'hello"));
        CHECK(!repl.IsComplete("console.log("));
    }

    SECTION("14.4 Error Formatter") {
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
        CHECK(formatted.find("TypeError") != std::string::npos);
        CHECK(formatted.find("Cannot read") != std::string::npos);
        CHECK(formatted.find("at ") != std::string::npos);

        // Simple format
        std::string simple = formatter.FormatSimple("Error", "Something went wrong");
        CHECK(simple.find("Error:") != std::string::npos);
    }

    SECTION("14.5 Profiler Timing") {
        Profiler profiler;

        // Begin/End
        profiler.Begin("test_operation", "test");
        std::this_thread::sleep_for(10ms);
        profiler.End("test_operation");

        auto entries = profiler.GetEntries("test");
        CHECK(entries.size() == 1);
        CHECK(entries[0].DurationMs() >= 5.0); // At least 5ms

        // Mark
        profiler.Mark("checkpoint", "marker");
        auto markers = profiler.GetEntries("marker");
        CHECK(markers.size() == 1);

        // Stats
        auto stats = profiler.GetStats("test_operation");
        CHECK(stats.total_entries == 1);
        CHECK(stats.total_time_ms > 0);
    }

    SECTION("14.6 Profiler Report and Control") {
        Profiler profiler;

        profiler.Begin("op1", "category_a");
        profiler.End("op1");
        profiler.Begin("op2", "category_b");
        profiler.End("op2");

        // Generate report
        std::string report = profiler.GenerateReport();
        CHECK(report.find("Profiler Report") != std::string::npos);
        CHECK(report.find("category_a") != std::string::npos);

        // Enable/Disable
        profiler.Disable();
        CHECK(!profiler.IsEnabled());

        profiler.Enable();
        CHECK(profiler.IsEnabled());

        // Clear
        profiler.Clear();
        CHECK(profiler.GetEntries().empty());
    }
}

TEST_CASE("Test 15: Expression Engine", "[expression]") {
    SECTION("15.1 ExpressionValue Types") {
        ExpressionValue null_val;
        ExpressionValue bool_val(true);
        ExpressionValue num_val(42.5);
        ExpressionValue str_val("hello");
        ExpressionValue arr_val(ExpressionValue::ArrayType{ExpressionValue(1), ExpressionValue(2)});

        CHECK(null_val.IsNull());
        CHECK(null_val.AsString() == "null");

        CHECK(bool_val.IsBoolean());
        CHECK(bool_val.AsBoolean() == true);

        CHECK(num_val.IsNumber());
        CHECK(num_val.AsNumber() == 42.5);

        CHECK(str_val.IsString());
        CHECK(str_val.AsString() == "hello");

        CHECK(arr_val.IsArray());
        CHECK(arr_val.AsArray().size() == 2);

        // Type conversions
        CHECK(ExpressionValue("123").AsNumber() == 123);
        CHECK(ExpressionValue(0).AsBoolean() == false);
        CHECK(ExpressionValue(1).AsBoolean() == true);
    }

    SECTION("15.2 ExpressionContext") {
        ExpressionContext ctx;

        ctx.Set("x", ExpressionValue(10));
        ctx.Set("y", ExpressionValue(20));
        ctx.Set("name", ExpressionValue("test"));

        CHECK(ctx.Has("x"));
        CHECK(ctx.Has("y"));
        CHECK(!ctx.Has("z"));
        CHECK(ctx.Get("x")->AsNumber() == 10);
        CHECK(ctx.GetNames().size() == 3);

        // Merge
        ExpressionContext ctx2;
        ctx2.Set("z", ExpressionValue(30));
        ctx.Merge(ctx2);
        CHECK(ctx.Has("z"));

        // Child context
        auto child = ctx.CreateChild();
        child.Set("w", ExpressionValue(40));
        CHECK(child.Has("x"));
        CHECK(child.Has("w"));
    }

    SECTION("15.3 Expression Arithmetic") {
        ExpressionEngine engine;

        auto r1 = engine.Evaluate("10 + 5");
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 15);

        auto r2 = engine.Evaluate("20 - 7");
        CHECK(r2.success);
        CHECK(r2.value.AsNumber() == 13);

        auto r3 = engine.Evaluate("6 * 7");
        CHECK(r3.success);
        CHECK(r3.value.AsNumber() == 42);

        auto r4 = engine.Evaluate("100 / 4");
        CHECK(r4.success);
        CHECK(r4.value.AsNumber() == 25);

        // With context
        ExpressionContext ctx;
        ctx.Set("a", ExpressionValue(100));
        ctx.Set("b", ExpressionValue(50));

        auto r5 = engine.Evaluate("a + b", ctx);
        CHECK(r5.success);
        CHECK(r5.value.AsNumber() == 150);
    }

    SECTION("15.4 Built-in Functions") {
        ExpressionEngine engine;

        // Math functions
        auto r1 = engine.Evaluate("abs(-5)");
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 5);

        auto r2 = engine.Evaluate("max(1, 5, 3)");
        CHECK(r2.success);
        CHECK(r2.value.AsNumber() == 5);

        auto r3 = engine.Evaluate("min(10, 2, 8)");
        CHECK(r3.success);
        CHECK(r3.value.AsNumber() == 2);

        auto r4 = engine.Evaluate("clamp(15, 0, 10)");
        CHECK(r4.success);
        CHECK(r4.value.AsNumber() == 10);

        // String functions
        auto r5 = engine.Evaluate("strlen('hello')");
        CHECK(r5.success);
        CHECK(r5.value.AsNumber() == 5);

        auto r6 = engine.Evaluate("upper('hello')");
        CHECK(r6.success);
        CHECK(r6.value.AsString() == "HELLO");
    }

    SECTION("15.5 Expression Validation") {
        ExpressionEngine engine;
        std::string error;

        CHECK(engine.Validate("1 + 2", error));
        CHECK(engine.Validate("(a + b) * c", error));

        CHECK(!engine.Validate("let x = 'unclosed", error));
        CHECK(!engine.Validate("((a + b)", error));
        CHECK(!engine.Validate("", error));
    }

    SECTION("15.6 Expression Caching") {
        ExpressionEngine engine;

        // Evaluate same expression multiple times
        engine.Evaluate("1 + 2 + 3");
        engine.Evaluate("1 + 2 + 3");
        engine.Evaluate("1 + 2 + 3");
        engine.Evaluate("4 + 5");

        CHECK(engine.GetCache().Size() == 2);
        CHECK(engine.GetCache().HitRate() > 0.4); // 2 hits out of 4 calls

        engine.GetCache().Clear();
        CHECK(engine.GetCache().Size() == 0);
    }
}

TEST_CASE("Test 16: Enhanced Expressions", "[expression][enhanced]") {
    SECTION("16.1 Variable Declarations") {
        ExpressionEngine engine;

        auto r1 = engine.Evaluate("let x = 10; x + 5;");
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 15);

        auto r2 = engine.Evaluate("const y = 20; y * 2;");
        CHECK(r2.success);
        CHECK(r2.value.AsNumber() == 40);

        auto r3 = engine.Evaluate("const z = 5; z = 6;");
        CHECK(!r3.success);
        CHECK(r3.error.find("const") != std::string::npos);
    }

    SECTION("16.2 Assignments and Sequencing") {
        ExpressionEngine engine;

        auto r1 = engine.Evaluate("let a = 1; a = a + 1; a = a * 2; a;");
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 4);

        // Multiple statements (return last value)
        auto r2 = engine.Evaluate("1+1; 2+2; 3+3;");
        CHECK(r2.success);
        CHECK(r2.value.AsNumber() == 6);
    }

    SECTION("16.3 String Interpolation") {
        ExpressionEngine engine;

        auto r1 = engine.Evaluate("let name = 'World'; \"Hello ${name}!\";");
        CHECK(r1.success);
        CHECK(r1.value.AsString() == "Hello World!");

        auto r2 = engine.Evaluate("let x = 5; \"Count: ${x}\";");
        CHECK(r2.success);
        CHECK(r2.value.AsString() == "Count: 5");

        auto r3 = engine.Evaluate("let missing = 'foo'; \"Val: ${bar}\";");
        CHECK(r3.success);
        CHECK(r3.value.AsString() == "Val: undefined");
    }

    SECTION("16.4 Whitespace and Comments") {
        ExpressionEngine engine;

        std::string code = "let x=10; let y=20; x+y;";
        auto r1 = engine.Evaluate(code);
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 30);
    }

    SECTION("16.5 Complex Logic") {
        ExpressionEngine engine;
        ExpressionContext ctx;

        auto r1 = engine.Evaluate("2 + 3 * 4", ctx);
        CHECK(r1.success);
        CHECK(r1.value.AsNumber() == 14);

        auto r2 = engine.Evaluate("(2 + 3) * 4", ctx);
        CHECK(r2.success);
        CHECK(r2.value.AsNumber() == 20);
    }
}

TEST_CASE("Test 17: Background Services", "[services]") {
    SECTION("17.1 Cron Parsing") {
        CronExpression every_min("* * * * *");
        CHECK(every_min.IsValid());
        CHECK(every_min.IsMatch(chrono::system_clock::now()));

        CronExpression specific("30 14 1 1 *");
        CHECK(specific.IsValid());

        CronExpression invalid("60 * * * *");
        CHECK(!invalid.IsValid());

        CronExpression step("*/5 * * * *");
        CHECK(step.IsValid());
    }

    SECTION("17.2 Service Worker Lifecycle") {
        ServiceWorker::Config config;
        config.script_path = "worker.js";

        ServiceWorker worker("sw-1", config);

        CHECK(worker.GetState() == ServiceWorker::State::Redundant);

        worker.Start();
        CHECK(worker.GetState() == ServiceWorker::State::Active);

        worker.DispatchEvent("fetch", "{}");
        CHECK(worker.GetEventCount() == 1);

        worker.Stop();
        CHECK(worker.GetState() == ServiceWorker::State::Redundant);
    }

    SECTION("17.3 Scheduler Job Registration") {
        BackgroundScheduler scheduler;

        bool added = scheduler.ScheduleJob("job1", "* * * * *", [] {});
        CHECK(added);

        bool bad_cron = !scheduler.ScheduleJob("job2", "invalid", [] {});
        CHECK(bad_cron);

        CHECK(scheduler.GetJobCount() == 1);
    }

    SECTION("17.4 Background Worker Registration") {
        BackgroundScheduler scheduler;

        ServiceWorker::Config config;
        config.auto_start = true;

        auto worker = scheduler.RegisterWorker("bg-worker-1", config);

        CHECK(scheduler.GetWorker("bg-worker-1") != nullptr);
        CHECK(worker->GetState() == ServiceWorker::State::Active);
    }
}

//=============================================================================
// NEW FEATURE TESTS (18-23)
//=============================================================================

TEST_CASE("Test 18: Result Monads", "[core][result]") {
    SECTION("18.1 Map") {
        Result<int> ok(42);
        auto mapped = ok.Map([](int x) { return x * 2; });
        REQUIRE(mapped.IsOk());
        CHECK(mapped.Value() == 84);

        Result<int> err(ScriptError::Make(ErrorCode::RuntimeError, "fail"));
        auto mapped_err = err.Map([](int x) { return x * 2; });
        REQUIRE(mapped_err.IsError());
        CHECK(mapped_err.Error().code == ErrorCode::RuntimeError);
    }

    SECTION("18.2 FlatMap") {
        auto divide = [](int x) -> Result<int> {
            if (x == 0) return ScriptError::Make(ErrorCode::InvalidArgument, "div by zero");
            return 100 / x;
        };

        Result<int> ok(5);
        auto result = ok.FlatMap(divide);
        REQUIRE(result.IsOk());
        CHECK(result.Value() == 20);

        Result<int> zero(0);
        auto fail_result = zero.FlatMap(divide);
        REQUIRE(fail_result.IsError());
    }

    SECTION("18.3 MapError") {
        Result<int> err(ScriptError::Make(ErrorCode::RuntimeError, "original"));
        auto mapped = err.MapError([](const ScriptError& e) {
            return ScriptError::Make(ErrorCode::InternalError, "wrapped: " + e.message);
        });
        REQUIRE(mapped.IsError());
        CHECK(mapped.Error().code == ErrorCode::InternalError);
        CHECK(mapped.Error().message.find("wrapped") != std::string::npos);
    }

    SECTION("18.4 Result<void>") {
        Result<void> ok = Result<void>::Ok();
        CHECK(ok.IsOk());
        CHECK(!ok.IsError());

        Result<void> err = Result<void>::Err(ScriptError::Make(ErrorCode::FileNotFound, "missing"));
        CHECK(err.IsError());
        CHECK(err.Error().code == ErrorCode::FileNotFound);
    }
}

TEST_CASE("Test 19: Logger Features", "[core][logger]") {
    SECTION("19.1 Log Format") {
        auto& logger = Logger::Instance();
        auto original = logger.GetFormat();

        logger.SetFormat(LogFormat::Json);
        CHECK(logger.GetFormat() == LogFormat::Json);

        logger.SetFormat(LogFormat::Text);
        CHECK(logger.GetFormat() == LogFormat::Text);

        logger.SetFormat(original);
    }

    SECTION("19.2 Log Entry") {
        LogEntry entry(LogLevel::Info, "test", "message");
        CHECK(entry.level == LogLevel::Info);
        CHECK(entry.category == "test");
        CHECK(entry.message == "message");
    }

    SECTION("19.3 Formatters") {
        LogEntry entry(LogLevel::Warn, "cat", "msg");

        TextLogFormatter text;
        std::string text_out = text.Format(entry);
        CHECK(text_out.find("[WARN]") != std::string::npos);
        CHECK(text_out.find("[cat]") != std::string::npos);

        JsonLogFormatter json;
        std::string json_out = json.Format(entry);
        CHECK(json_out.find("\"level\":\"WARN\"") != std::string::npos);
        CHECK(json_out.find("\"category\":\"cat\"") != std::string::npos);
    }

    SECTION("19.4 AsyncLogger") {
        AsyncLogger async;
        std::atomic<int> count{0};

        async.SetCallback([&count](const LogEntry&) { count++; });
        async.SetMinLevel(LogLevel::Info);
        async.Start();

        CHECK(async.IsRunning());

        async.Log(LogLevel::Info, "test", "msg1");
        async.Log(LogLevel::Info, "test", "msg2");

        async.Flush();
        async.Stop();

        CHECK(!async.IsRunning());
        CHECK(count >= 2);
    }
}

TEST_CASE("Test 20: Script Metadata", "[script][metadata]") {
    SECTION("20.1 UUID Generation") {
        auto script1 = std::make_shared<Script>("'test';");
        auto script2 = std::make_shared<Script>("'test';");

        std::string id1 = script1->GetName();
        std::string id2 = script2->GetName();

        CHECK(id1 != id2);
        CHECK(id1.find("-") != std::string::npos);  // UUID format has dashes
    }

    SECTION("20.2 Metadata CRUD") {
        auto script = std::make_shared<Script>("'test';");

        script->SetMetadata("key1", "value1");
        script->SetMetadata("key2", "value2");

        CHECK(script->HasMetadata("key1"));
        CHECK(script->HasMetadata("key2"));
        CHECK(!script->HasMetadata("key3"));

        auto val1 = script->GetMetadata("key1");
        REQUIRE(val1.has_value());
        CHECK(*val1 == "value1");

        auto val3 = script->GetMetadata("key3");
        CHECK(!val3.has_value());
    }

    SECTION("20.3 GetAllMetadata") {
        auto script = std::make_shared<Script>("'test';");
        script->SetMetadata("a", "1");
        script->SetMetadata("b", "2");

        auto all = script->GetAllMetadata();
        CHECK(all.size() == 2);
        CHECK(all["a"] == "1");
        CHECK(all["b"] == "2");
    }

    SECTION("20.4 ClearMetadata") {
        auto script = std::make_shared<Script>("'test';");
        script->SetMetadata("key", "val");
        CHECK(script->HasMetadata("key"));

        script->ClearMetadata();
        CHECK(!script->HasMetadata("key"));
    }
}

TEST_CASE("Test 21: Thread Safety Utilities", "[core][thread]") {
    SECTION("21.1 ThreadSafeMap Basic") {
        ThreadSafeMap<std::string, int> map;

        map.Set("a", 1);
        map.Set("b", 2);

        CHECK(map.Has("a"));
        CHECK(map.Has("b"));
        CHECK(!map.Has("c"));

        auto val = map.Get("a");
        REQUIRE(val.has_value());
        CHECK(*val == 1);

        CHECK(map.GetOr("c", 99) == 99);
        CHECK(map.Size() == 2);
    }

    SECTION("21.2 ThreadSafeMap Remove/Clear") {
        ThreadSafeMap<int, std::string> map;
        map.Set(1, "one");
        map.Set(2, "two");

        CHECK(map.Remove(1));
        CHECK(!map.Has(1));
        CHECK(map.Size() == 1);

        map.Clear();
        CHECK(map.Empty());
    }

    SECTION("21.3 ThreadSafeMap Concurrent") {
        ThreadSafeMap<int, int> map;
        std::vector<std::thread> threads;

        for (int i = 0; i < 10; i++) {
            threads.emplace_back([&map, i] {
                map.Set(i, i * 10);
                std::this_thread::sleep_for(1ms);
                map.Get(i);
            });
        }

        for (auto& t : threads) t.join();

        CHECK(map.Size() == 10);
    }

    SECTION("21.4 ThreadSafeValue") {
        ThreadSafeValue<int> val(0);

        CHECK(val.Get() == 0);

        val.Set(42);
        CHECK(val.Get() == 42);

        val.Apply([](int& v) { v *= 2; });
        CHECK(val.Get() == 84);
    }
}

TEST_CASE("Test 22: Expression Operators", "[expression][operators]") {
    ExpressionEngine engine;

    SECTION("22.1 Comparison Operators") {
        CHECK(engine.Evaluate("5 < 10").value.AsBoolean() == true);
        CHECK(engine.Evaluate("10 < 5").value.AsBoolean() == false);
        CHECK(engine.Evaluate("5 <= 5").value.AsBoolean() == true);
        CHECK(engine.Evaluate("5 > 3").value.AsBoolean() == true);
        CHECK(engine.Evaluate("5 >= 5").value.AsBoolean() == true);
        CHECK(engine.Evaluate("5 >= 6").value.AsBoolean() == false);
    }

    SECTION("22.2 Logical AND") {
        CHECK(engine.Evaluate("true && true").value.AsBoolean() == true);
        CHECK(engine.Evaluate("true && false").value.AsBoolean() == false);
        CHECK(engine.Evaluate("false && true").value.AsBoolean() == false);
        CHECK(engine.Evaluate("false && false").value.AsBoolean() == false);
    }

    SECTION("22.3 Logical OR") {
        CHECK(engine.Evaluate("true || true").value.AsBoolean() == true);
        CHECK(engine.Evaluate("true || false").value.AsBoolean() == true);
        CHECK(engine.Evaluate("false || true").value.AsBoolean() == true);
        CHECK(engine.Evaluate("false || false").value.AsBoolean() == false);
    }

    SECTION("22.4 Logical NOT") {
        CHECK(engine.Evaluate("!true").value.AsBoolean() == false);
        CHECK(engine.Evaluate("!false").value.AsBoolean() == true);
        CHECK(engine.Evaluate("!!true").value.AsBoolean() == true);
    }

    SECTION("22.5 Combined Logic") {
        CHECK(engine.Evaluate("(5 > 3) && (10 < 20)").value.AsBoolean() == true);
        CHECK(engine.Evaluate("(5 < 3) || (10 > 5)").value.AsBoolean() == true);
        CHECK(engine.Evaluate("!(5 > 10)").value.AsBoolean() == true);
    }
}

TEST_CASE("Test 23: Sandbox Path Security", "[isolation][sandbox]") {
    SECTION("23.1 NormalizePath") {
        std::string norm = SandboxGuard::NormalizePath(".");
        CHECK(!norm.empty());

        // Non-existent path should still normalize
        std::string fake = SandboxGuard::NormalizePath("./fake/path/file.txt");
        CHECK(!fake.empty());
    }

    SECTION("23.2 IsPathContained") {
        // Same directory should be contained
        std::string cwd = SandboxGuard::NormalizePath(".");
        CHECK(SandboxGuard::IsPathContained(cwd, cwd));
    }

    SECTION("23.3 Traversal Detection") {
        SandboxConfig config;
        config.allowed_read_paths = {"./safe"};
        SandboxGuard guard(config);

        // If you try to read from a path with .., it should detect
        // Note: exact behavior depends on file system state
        CHECK(!guard.CheckFileRead("./safe/../../../etc/passwd"));
    }

    SECTION("23.4 Blocked Paths") {
        SandboxConfig config;
        config.blocked_paths = {"./blocked"};
        SandboxGuard guard(config);

        CHECK(!guard.CheckFileRead("./blocked/file.txt"));
    }
}

//=============================================================================
// EXTENDED FEATURE TESTS (24-29)
//=============================================================================

TEST_CASE("Test 24: ExpressionValue Operators", "[expression][operators]") {
    SECTION("24.1 Arithmetic Operators") {
        ExpressionValue a(10.0);
        ExpressionValue b(3.0);
        
        CHECK((a + b).AsNumber() == 13.0);
        CHECK((a - b).AsNumber() == 7.0);
        CHECK((a * b).AsNumber() == 30.0);
        CHECK((a / b).AsNumber() == Catch::Approx(3.333).epsilon(0.01));
        CHECK((a % b).AsNumber() == 1.0);
        CHECK((-a).AsNumber() == -10.0);
    }
    
    SECTION("24.2 Comparison Operators") {
        ExpressionValue a(5.0);
        ExpressionValue b(10.0);
        
        CHECK(a < b);
        CHECK(a <= b);
        CHECK(b > a);
        CHECK(b >= a);
        CHECK(a <= a);
        CHECK(a >= a);
    }
    
    SECTION("24.3 Logical Operators") {
        ExpressionValue t(true);
        ExpressionValue f(false);
        
        CHECK((t && t) == true);
        CHECK((t && f) == false);
        CHECK((t || f) == true);
        CHECK((f || f) == false);
        CHECK(!f == true);
        CHECK(!t == false);
    }
    
    SECTION("24.4 String Concatenation") {
        ExpressionValue s1("Hello");
        ExpressionValue s2(" World");
        
        auto result = s1 + s2;
        CHECK(result.AsString() == "Hello World");
    }
}

TEST_CASE("Test 25: Async Expression Evaluation", "[expression][async]") {
    ExpressionEngine engine;
    
    SECTION("25.1 EvaluateAsync") {
        auto future = engine.EvaluateAsync("1 + 2 * 3");
        auto result = future.get();
        
        CHECK(result.success);
        CHECK(result.value.AsNumber() == 7.0);
    }
    
    SECTION("25.2 EvaluateBatchAsync") {
        std::vector<std::string> exprs = {"1+1", "2+2", "3+3"};
        auto futures = engine.EvaluateBatchAsync(exprs);
        
        CHECK(futures.size() == 3);
        
        auto r1 = futures[0].get();
        auto r2 = futures[1].get();
        auto r3 = futures[2].get();
        
        CHECK(r1.value.AsNumber() == 2.0);
        CHECK(r2.value.AsNumber() == 4.0);
        CHECK(r3.value.AsNumber() == 6.0);
    }
    
    SECTION("25.3 Compile and Hot Tracking") {
        auto compiled = engine.Compile("100 / 5");
        REQUIRE(compiled.has_value());
        
        CHECK(compiled->GetUsageCount() == 0);
        CHECK(!compiled->IsHot());
        
        ExpressionContext ctx;
        for (int i = 0; i < 15; ++i) {
            engine.EvaluateCompiled(*compiled, ctx);
        }
        
        CHECK(compiled->GetUsageCount() == 15);
        CHECK(compiled->IsHot());
    }
}

TEST_CASE("Test 26: Script Dependencies", "[script][dependencies]") {
    SECTION("26.1 Add and Check Dependencies") {
        auto script = std::make_shared<Script>("'test';");
        
        script->AddDependency("lodash");
        script->AddDependency("react");
        script->AddDependency("lodash"); // duplicate
        
        CHECK(script->HasDependency("lodash"));
        CHECK(script->HasDependency("react"));
        CHECK(!script->HasDependency("vue"));
        CHECK(script->DependencyCount() == 2); // no duplicates
    }
    
    SECTION("26.2 GetDependencies") {
        auto script = std::make_shared<Script>("'test';");
        script->AddDependency("a");
        script->AddDependency("b");
        script->AddDependency("c");
        
        auto deps = script->GetDependencies();
        CHECK(deps.size() == 3);
    }
    
    SECTION("26.3 ClearDependencies") {
        auto script = std::make_shared<Script>("'test';");
        script->AddDependency("dep1");
        CHECK(script->DependencyCount() == 1);
        
        script->ClearDependencies();
        CHECK(script->DependencyCount() == 0);
    }
}

TEST_CASE("Test 27: File Log Sink", "[logger][file]") {
    SECTION("27.1 Options") {
        FileLogSink::Options opts;
        opts.base_path = "test.log";
        opts.max_file_size = 1024;
        opts.max_files = 3;
        opts.format = LogFormat::Json;
        
        FileLogSink sink(opts);
        CHECK(sink.GetOptions().max_file_size == 1024);
        CHECK(sink.GetOptions().max_files == 3);
    }
    
    SECTION("27.2 Write Entry") {
        FileLogSink::Options opts;
        opts.base_path = "test_write.log";
        opts.max_file_size = 10000;
        
        {
            FileLogSink sink(opts);
            LogEntry entry(LogLevel::Info, "test", "Test message");
            sink.Write(entry);
            CHECK(sink.GetCurrentSize() > 0);
        }
        
        // Cleanup
        std::remove("test_write.log");
    }
}

TEST_CASE("Test 28: Remote Loader Retry", "[modules][retry]") {
    SECTION("28.1 Retry Options") {
        RemoteLoader::RetryOptions opts;
        opts.max_retries = 5;
        opts.initial_delay = 50ms;
        opts.backoff_factor = 1.5;
        
        RemoteLoader loader(std::chrono::seconds(60), opts);
        CHECK(loader.GetRetryOptions().max_retries == 5);
    }
    
    SECTION("28.2 Progress Callback") {
        RemoteLoader loader;
        bool called = false;
        
        loader.SetProgressCallback([&called](size_t, size_t, const std::string&) {
            called = true;
        });
        
        // Load will fail (placeholder), but callback should be invoked
        loader.Load("https://example.com/test.js");
        CHECK(called);
    }
}

TEST_CASE("Test 29: Thread-Safe Iteration", "[thread][iteration]") {
    ThreadSafeMap<std::string, int> map;
    map.Set("a", 1);
    map.Set("b", 2);
    map.Set("c", 3);
    
    SECTION("29.1 LockedForEach") {
        int sum = 0;
        map.LockedForEach([&sum](const std::string&, int v) {
            sum += v;
            return false; // continue
        });
        CHECK(sum == 6);
    }
    
    SECTION("29.2 LockedForEach with early exit") {
        std::string found;
        map.LockedForEach([&found](const std::string& k, int v) {
            if (v == 2) {
                found = k;
                return true; // break
            }
            return false;
        });
        CHECK(found == "b");
    }
    
    SECTION("29.3 Collect") {
        auto filtered = map.Collect([](const std::string&, int v) {
            return v > 1;
        });
        CHECK(filtered.size() == 2);
    }
    
    SECTION("29.4 GetLockedView") {
        size_t count = 0;
        {
            auto view = map.GetLockedView();
            for (const auto& [k, v] : view) {
                count++;
            }
        }
        CHECK(count == 3);
    }
}

TEST_CASE("Test 30: Reactive Expressions", "[expression][reactive]") {
    SECTION("30.1 ReactiveValue Basics") {
        ReactiveContext ctx;
        
        auto x = ctx.CreateValue("x", ExpressionValue(10.0));
        CHECK(x->Get().AsNumber() == 10.0);
        
        x->Set(ExpressionValue(20.0));
        CHECK(x->Get().AsNumber() == 20.0);
        CHECK(x->GetVersion() > 0);
    }
    
    SECTION("30.2 ReactiveValue Subscription") {
        ReactiveContext ctx;
        auto x = ctx.CreateValue("x", ExpressionValue(5.0));
        
        double notified_value = 0;
        auto sub_id = x->Subscribe([&notified_value](const ExpressionValue& v) {
            notified_value = v.AsNumber();
        });
        
        x->Set(ExpressionValue(15.0));
        CHECK(notified_value == 15.0);
        
        x->Unsubscribe(sub_id);
        x->Set(ExpressionValue(25.0));
        CHECK(notified_value == 15.0);  // Not updated after unsubscribe
    }
    
    SECTION("30.3 ReactiveExpression Auto-Update") {
        ReactiveContext ctx;
        ctx.Set("x", ExpressionValue(10.0));
        ctx.Set("y", ExpressionValue(20.0));
        
        auto sum = ctx.CreateExpression("x + y");
        CHECK(sum->IsValid());
        CHECK(sum->Get().AsNumber() == 30.0);
        
        // Change x
        ctx.Set("x", ExpressionValue(15.0));
        CHECK(sum->Get().AsNumber() == 35.0);
        
        // Change y
        ctx.Set("y", ExpressionValue(25.0));
        CHECK(sum->Get().AsNumber() == 40.0);
    }
    
    SECTION("30.4 ReactiveExpression OnChange Callback") {
        ReactiveContext ctx;
        ctx.Set("a", ExpressionValue(5.0));
        ctx.Set("b", ExpressionValue(3.0));
        
        auto product = ctx.CreateExpression("a * b");
        
        std::vector<double> changes;
        product->OnChange([&changes](const ExpressionValue& v) {
            changes.push_back(v.AsNumber());
        });
        
        ctx.Set("a", ExpressionValue(10.0));  // 10 * 3 = 30
        ctx.Set("b", ExpressionValue(4.0));   // 10 * 4 = 40
        
        REQUIRE(changes.size() >= 2);
        CHECK(changes.back() == 40.0);
    }
    
    SECTION("30.5 Batch Updates") {
        ReactiveContext ctx;
        ctx.Set("x", ExpressionValue(1.0));
        
        auto expr = ctx.CreateExpression("x * 2");
        
        int update_count = 0;
        expr->OnChange([&update_count](const ExpressionValue&) {
            update_count++;
        });
        
        // Without batch: each Set triggers an update
        ctx.Set("x", ExpressionValue(2.0));
        ctx.Set("x", ExpressionValue(3.0));
        int individual_updates = update_count;
        
        // With batch: updates are deferred
        update_count = 0;
        ctx.Batch([&ctx]() {
            ctx.Set("x", ExpressionValue(4.0));
            ctx.Set("x", ExpressionValue(5.0));
            ctx.Set("x", ExpressionValue(6.0));
        });
        
        // Batch should result in fewer notifications
        CHECK(expr->Get().AsNumber() == 12.0);  // 6 * 2
    }
    
    SECTION("30.6 Get Dependencies") {
        ReactiveContext ctx;
        ctx.Set("width", ExpressionValue(10.0));
        ctx.Set("height", ExpressionValue(20.0));
        
        auto area = ctx.CreateExpression("width * height");
        auto deps = area->GetDependencies();
        
        CHECK(deps.size() >= 2);
        // Check that both are tracked
        bool has_width = std::find(deps.begin(), deps.end(), "width") != deps.end();
        bool has_height = std::find(deps.begin(), deps.end(), "height") != deps.end();
        CHECK(has_width);
        CHECK(has_height);
    }
    
    SECTION("30.7 Complex Expression Chain") {
        ReactiveContext ctx;
        ctx.Set("base", ExpressionValue(100.0));
        ctx.Set("rate", ExpressionValue(0.1));
        
        auto interest = ctx.CreateExpression("base * rate");
        auto total = ctx.CreateExpression("base + base * rate");
        
        CHECK(interest->Get().AsNumber() == 10.0);
        CHECK(total->Get().AsNumber() == 110.0);
        
        ctx.Set("base", ExpressionValue(200.0));
        CHECK(interest->Get().AsNumber() == 20.0);
        CHECK(total->Get().AsNumber() == 220.0);
        
        ctx.Set("rate", ExpressionValue(0.2));
        CHECK(interest->Get().AsNumber() == 40.0);
        CHECK(total->Get().AsNumber() == 240.0);
    }
}

TEST_CASE("Test 31: Operator Hooks", "[expression][hooks]") {
    SECTION("31.1 Basic Hook Set/Has/Get") {
        ExpressionValue val(42.0);
        
        CHECK(!val.HasHook(OperatorHook::Add));
        
        val.SetHook(OperatorHook::Add, [](const ExpressionValue& self, const std::vector<ExpressionValue>& args) {
            return ExpressionValue(self.AsNumber() + args[0].AsNumber() * 2);
        });
        
        CHECK(val.HasHook(OperatorHook::Add));
        CHECK(val.GetHook(OperatorHook::Add).has_value());
    }
    
    SECTION("31.2 CallHook") {
        ExpressionValue val(10.0);
        
        val.SetHook(OperatorHook::Neg, [](const ExpressionValue& self, const std::vector<ExpressionValue>&) {
            return ExpressionValue(-self.AsNumber() * 100);  // Custom negate
        });
        
        auto result = val.CallHook(OperatorHook::Neg);
        REQUIRE(result.has_value());
        CHECK(result->AsNumber() == -1000.0);
    }
    
    SECTION("31.3 ApplyBinaryHook with custom __add__") {
        ExpressionValue obj;
        obj.SetHook(OperatorHook::Add, [](const ExpressionValue& self, const std::vector<ExpressionValue>& args) {
            // Custom add: concatenate strings with " + "
            return ExpressionValue(self.AsString() + " + " + args[0].AsString());
        });
        
        ExpressionValue other("world");
        auto result = obj.ApplyBinaryHook(OperatorHook::Add, other, []() {
            return ExpressionValue("fallback");
        });
        
        CHECK(result.AsString() == "null + world");
    }
    
    SECTION("31.4 Conversion hooks __str__ and __bool__") {
        ExpressionValue::ObjectType data;
        data["name"] = ExpressionValue("MyObject");
        data["active"] = ExpressionValue(true);
        
        ExpressionValue obj(data);
        
        obj.SetHook(OperatorHook::Str, [](const ExpressionValue& self, const std::vector<ExpressionValue>&) {
            auto& o = self.AsObject();
            auto it = o.find("name");
            if (it != o.end()) return ExpressionValue("<Object: " + it->second.AsString() + ">");
            return ExpressionValue("<Object>");
        });
        
        obj.SetHook(OperatorHook::Bool, [](const ExpressionValue& self, const std::vector<ExpressionValue>&) {
            auto& o = self.AsObject();
            auto it = o.find("active");
            return ExpressionValue(it != o.end() && it->second.AsBoolean());
        });
        
        auto str_result = obj.CallHook(OperatorHook::Str);
        REQUIRE(str_result.has_value());
        CHECK(str_result->AsString() == "<Object: MyObject>");
        
        auto bool_result = obj.CallHook(OperatorHook::Bool);
        REQUIRE(bool_result.has_value());
        CHECK(bool_result->AsBoolean() == true);
    }
    
    SECTION("31.5 Custom Vector type with __add__ and __len__") {
        // Create a "Vector" object
        ExpressionValue::ArrayType vec_data = {ExpressionValue(1.0), ExpressionValue(2.0), ExpressionValue(3.0)};
        ExpressionValue vec(vec_data);
        
        // __add__: element-wise addition
        vec.SetHook(OperatorHook::Add, [](const ExpressionValue& self, const std::vector<ExpressionValue>& args) {
            auto& arr1 = self.AsArray();
            auto& arr2 = args[0].AsArray();
            
            ExpressionValue::ArrayType result;
            size_t len = std::min(arr1.size(), arr2.size());
            for (size_t i = 0; i < len; ++i) {
                result.push_back(ExpressionValue(arr1[i].AsNumber() + arr2[i].AsNumber()));
            }
            return ExpressionValue(result);
        });
        
        // __len__: return array length
        vec.SetHook(OperatorHook::Len, [](const ExpressionValue& self, const std::vector<ExpressionValue>&) {
            return ExpressionValue(static_cast<double>(self.AsArray().size()));
        });
        
        // Test __len__
        auto len_result = vec.CallHook(OperatorHook::Len);
        REQUIRE(len_result.has_value());
        CHECK(len_result->AsNumber() == 3.0);
        
        // Test __add__
        ExpressionValue::ArrayType vec2_data = {ExpressionValue(10.0), ExpressionValue(20.0), ExpressionValue(30.0)};
        ExpressionValue vec2(vec2_data);
        
        auto add_result = vec.CallHook(OperatorHook::Add, {vec2});
        REQUIRE(add_result.has_value());
        
        auto& sum_arr = add_result->AsArray();
        REQUIRE(sum_arr.size() == 3);
        CHECK(sum_arr[0].AsNumber() == 11.0);
        CHECK(sum_arr[1].AsNumber() == 22.0);
        CHECK(sum_arr[2].AsNumber() == 33.0);
    }
    
    SECTION("31.6 OperatorHookName") {
        CHECK(std::string(OperatorHookName(OperatorHook::Add)) == "__add__");
        CHECK(std::string(OperatorHookName(OperatorHook::Eq)) == "__eq__");
        CHECK(std::string(OperatorHookName(OperatorHook::Call)) == "__call__");
        CHECK(std::string(OperatorHookName(OperatorHook::Len)) == "__len__");
    }
}

//=============================================================================
// Test 32: ScriptResult API
//=============================================================================

TEST_CASE("Test 32: ScriptResult API", "[script][result]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("32.1 Number result") {
        auto script = engine.CreateScript("42.5;", {.name = "number-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& result = script->GetResultValue();
        CHECK(result.IsNumber());
        CHECK(!result.IsString());
        CHECK(!result.IsObject());
        
        auto num = result.ToNumber();
        REQUIRE(num.has_value());
        CHECK(*num == Catch::Approx(42.5));
        
        auto int64 = result.ToInt64();
        REQUIRE(int64.has_value());
        CHECK(*int64 == 42);
    }
    
    SECTION("32.2 String result") {
        auto script = engine.CreateScript("'hello world';", {.name = "string-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& result = script->GetResultValue();
        CHECK(result.IsString());
        CHECK(!result.IsNumber());
        
        CHECK(result.ToString() == "hello world");
        CHECK(result.GetStringResult() == "hello world");
    }
    
    SECTION("32.3 Boolean result") {
        auto script = engine.CreateScript("true;", {.name = "bool-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& result = script->GetResultValue();
        CHECK(result.IsBoolean());
        
        auto boolVal = result.ToBool();
        REQUIRE(boolVal.has_value());
        CHECK(*boolVal == true);
    }
    
    SECTION("32.4 Object result") {
        auto script = engine.CreateScript("({a: 1, b: 'test'});", {.name = "object-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& result = script->GetResultValue();
        CHECK(result.IsObject());
        CHECK(!result.IsArray());
        
        // ToString should return JSON-like representation
        auto str = result.ToString();
        CHECK(str.find("object") != std::string::npos);
    }
    
    SECTION("32.5 Array result") {
        auto script = engine.CreateScript("[1, 2, 3];", {.name = "array-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& result = script->GetResultValue();
        CHECK(result.IsArray());
        CHECK(result.IsObject()); // Arrays are objects in JS
    }
    
    SECTION("32.6 Null and undefined") {
        auto scriptNull = engine.CreateScript("null;", {.name = "null-test"});
        auto scriptUndef = engine.CreateScript("undefined;", {.name = "undef-test"});
        auto mainEnv = engine.GetMainEnvironment();
        
        mainEnv->Execute(scriptNull);
        mainEnv->Execute(scriptUndef);
        scriptNull->Wait(5s);
        scriptUndef->Wait(5s);
        
        REQUIRE(scriptNull->HasResultValue());
        REQUIRE(scriptUndef->HasResultValue());
        
        CHECK(scriptNull->GetResultValue().IsNull());
        CHECK(scriptNull->GetResultValue().IsNullOrUndefined());
        
        CHECK(scriptUndef->GetResultValue().IsUndefined());
        CHECK(scriptUndef->GetResultValue().IsNullOrUndefined());
    }
    
    SECTION("32.7 Function result") {
        auto script = engine.CreateScript("(function test() { return 42; });", {.name = "func-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        CHECK(script->GetResultValue().IsFunction());
    }
    
    SECTION("32.8 Empty result on failure") {
        // Invalid script should fail and have no result value
        auto script = engine.CreateScript("syntax error !!!!", {.name = "fail-test"});
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        CHECK(script->GetState() == ScriptState::Failed);
        CHECK(!script->HasResultValue());
    }
}

//=============================================================================
// Test 33: ScriptResult Function Calling
//=============================================================================

TEST_CASE("Test 33: ScriptResult Function Calling", "[script][result][call]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("33.1 Call function with no args") {
        auto script = engine.CreateScript(
            "(function() { return 42; });",
            {.name = "func-noargs"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->GetState() == ScriptState::Completed);
        REQUIRE(script->HasResultValue());
        
        auto& func = script->GetResultValue();
        REQUIRE(func.IsFunction());
        
        auto result = func.Call();
        REQUIRE(result.HasValue());
        CHECK(result.IsNumber());
        CHECK(result.ToNumber().value_or(0) == 42);
    }
    
    SECTION("33.2 Call function with args") {
        auto script = engine.CreateScript(
            "(function(a, b) { return a + b; });",
            {.name = "func-args"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->HasResultValue());
        auto& func = script->GetResultValue();
        
        // Create arguments
        auto argScript1 = engine.CreateScript("10;", {.name = "arg1"});
        auto argScript2 = engine.CreateScript("20;", {.name = "arg2"});
        mainEnv->Execute(argScript1);
        mainEnv->Execute(argScript2);
        argScript1->Wait(5s);
        argScript2->Wait(5s);
        
        auto& arg1 = argScript1->GetResultValue();
        auto& arg2 = argScript2->GetResultValue();
        
        std::vector<ScriptValue*> args = {&arg1, &arg2};
        auto result = func.Call(args);
        
        REQUIRE(result.HasValue());
        CHECK(result.ToNumber().value_or(0) == 30);
    }
    
    SECTION("33.3 CallMethod on object") {
        auto script = engine.CreateScript(
            "({ value: 10, double: function() { return this.value * 2; } });",
            {.name = "obj-method"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->HasResultValue());
        auto& obj = script->GetResultValue();
        REQUIRE(obj.IsObject());
        
        auto result = obj.CallMethod("double");
        REQUIRE(result.HasValue());
        CHECK(result.ToNumber().value_or(0) == 20);
    }
    
    SECTION("33.4 Get property from object") {
        auto script = engine.CreateScript(
            "({ name: 'test', count: 42 });",
            {.name = "obj-props"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->HasResultValue());
        auto& obj = script->GetResultValue();
        
        auto name = obj.Get("name");
        REQUIRE(name.HasValue());
        CHECK(name.IsString());
        CHECK(name.ToString() == "test");
        
        auto count = obj.Get("count");
        REQUIRE(count.HasValue());
        CHECK(count.ToNumber().value_or(0) == 42);
    }
    
    SECTION("33.5 Get element from array") {
        auto script = engine.CreateScript(
            "[10, 20, 30];",
            {.name = "array-elems"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->HasResultValue());
        auto& arr = script->GetResultValue();
        REQUIRE(arr.IsArray());
        
        auto len = arr.Length();
        REQUIRE(len.has_value());
        CHECK(*len == 3);
        
        auto elem0 = arr.Get(0u);
        CHECK(elem0.ToNumber().value_or(0) == 10);
        
        auto elem2 = arr.Get(2u);
        CHECK(elem2.ToNumber().value_or(0) == 30);
    }
    
    SECTION("33.6 Nested property access") {
        auto script = engine.CreateScript(
            "({ outer: { inner: { value: 'deep' } } });",
            {.name = "nested-props"}
        );
        auto mainEnv = engine.GetMainEnvironment();
        mainEnv->Execute(script);
        script->Wait(5s);
        
        REQUIRE(script->HasResultValue());
        auto& obj = script->GetResultValue();
        
        auto deep = obj.Get("outer").Get("inner").Get("value");
        REQUIRE(deep.HasValue());
        CHECK(deep.ToString() == "deep");
    }
}

//=============================================================================
// Test 34: ScriptResult API
//=============================================================================

TEST_CASE("Test 34: ScriptResult API", "[script][result]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("34.1 IsOk and IsError for success") {
        auto result = engine.ExecuteSync("42");
        
        CHECK(result.IsOk());
        CHECK_FALSE(result.IsError());
        CHECK(static_cast<bool>(result) == true);  // operator bool
    }
    
    SECTION("34.2 IsOk and IsError for failure") {
        auto result = engine.ExecuteSync("throw new Error('test error')");
        
        CHECK_FALSE(result.IsOk());
        CHECK(result.IsError());
        CHECK(static_cast<bool>(result) == false);
    }
    
    SECTION("34.3 Value access on success") {
        auto result = engine.ExecuteSync("'hello world'");
        
        REQUIRE(result.IsOk());
        ScriptValue& val = result.Value();
        CHECK(val.HasValue());
        CHECK(val.IsString());
        CHECK(val.ToString() == "hello world");
    }
    
    SECTION("34.4 Error access on failure") {
        auto result = engine.ExecuteSync("throw new Error('custom error')");
        
        REQUIRE(result.IsError());
        ScriptError& err = result.Error();
        CHECK(err.code == ErrorCode::RuntimeError);
        CHECK(err.message.find("custom error") != std::string::npos);
    }
    
    SECTION("34.5 Value throws on error") {
        auto result = engine.ExecuteSync("throw 'fail'");
        CHECK(result.IsError());
        CHECK_THROWS_AS(result.Value(), std::runtime_error);
    }
    
    SECTION("34.6 Error throws on success") {
        auto result = engine.ExecuteSync("123");
        CHECK(result.IsOk());
        CHECK_THROWS_AS(result.Error(), std::runtime_error);
    }
    
    SECTION("34.7 TryValue safe access") {
        auto success = engine.ExecuteSync("100");
        auto failure = engine.ExecuteSync("undefined_var.prop");
        
        CHECK(success.TryValue() != nullptr);
        CHECK(success.TryError() == nullptr);
        
        CHECK(failure.TryValue() == nullptr);
        CHECK(failure.TryError() != nullptr);
    }
    
    SECTION("34.8 ToString convenience") {
        auto numResult = engine.ExecuteSync("99.5");
        auto strResult = engine.ExecuteSync("'test string'");
        auto errResult = engine.ExecuteSync("null.x");
        
        CHECK(numResult.ToString() == "99.5");
        CHECK(strResult.ToString() == "test string");
        // On error, ToString returns error message
        CHECK(errResult.ToString().find("null") != std::string::npos);
    }
    
    SECTION("34.9 ToNumber and ToBool convenience") {
        auto numResult = engine.ExecuteSync("42.5");
        auto boolResult = engine.ExecuteSync("true");
        auto errResult = engine.ExecuteSync("throw 'x'");
        
        CHECK(numResult.ToNumber().value_or(-1) == 42.5);
        CHECK(boolResult.ToBool().value_or(false) == true);
        
        // On error, returns nullopt
        CHECK_FALSE(errResult.ToNumber().has_value());
        CHECK_FALSE(errResult.ToBool().has_value());
    }
    
    SECTION("34.10 Chained operations on ScriptValue") {
        auto result = engine.ExecuteSync("({ a: { b: { c: 42 } } })");
        
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        
        // Chain Get operations
        auto c = val.Get("a").Get("b").Get("c");
        CHECK(c.HasValue());
        CHECK(c.ToNumber().value_or(0) == 42);
    }
    
    SECTION("34.11 ScriptError stack trace") {
        auto result = engine.ExecuteSync(R"(
            function inner() { throw new Error('deep'); }
            function outer() { inner(); }
            outer();
        )");
        
        REQUIRE(result.IsError());
        auto& err = result.Error();
        CHECK(!err.stack.empty());
        CHECK(err.stack.find("inner") != std::string::npos);
        CHECK(err.stack.find("outer") != std::string::npos);
    }
}

//=============================================================================
// Test 35: ScriptResult Monadic Operations
//=============================================================================

TEST_CASE("Test 35: ScriptResult Monadic Operations", "[script][result][monad]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("35.1 Transform on success") {
        auto result = engine.ExecuteSync("42");
        REQUIRE(result.IsOk());
        
        // Transform applies function to value
        int transformed = 0;
        result.Transform([&](const ScriptValue& v) {
            transformed = static_cast<int>(v.ToNumber().value_or(0));
            return v;  // Must return ScriptValue
        });
        CHECK(transformed == 42);
    }
    
    SECTION("35.2 Transform propagates error") {
        auto result = engine.ExecuteSync("throw 'test'");
        REQUIRE(result.IsError());
        
        bool called = false;
        auto transformed = result.Transform([&](const ScriptValue& v) {
            called = true;
            return v;
        });
        
        CHECK_FALSE(called);  // Function not called on error
        CHECK(transformed.IsError());
    }
    
    SECTION("35.3 AndThen chains operations") {
        // Create a script that returns code to execute
        auto result = engine.ExecuteSync("'1 + 2'");
        REQUIRE(result.IsOk());
        
        // Chain to execute the returned code
        auto chained = result.AndThen([&](const ScriptValue& v) {
            return engine.ExecuteSync(v.ToString());
        });
        
        REQUIRE(chained.IsOk());
        CHECK(chained.ToNumber().value_or(0) == 3);
    }
    
    SECTION("35.4 AndThen short-circuits on error") {
        auto result = engine.ExecuteSync("throw 'first error'");
        
        bool called = false;
        auto chained = result.AndThen([&](const ScriptValue& v) {
            called = true;
            return engine.ExecuteSync("unreachable");
        });
        
        CHECK_FALSE(called);
        CHECK(chained.IsError());
    }
    
    SECTION("35.5 OrElse provides fallback") {
        auto result = engine.ExecuteSync("throw 'error'");
        REQUIRE(result.IsError());
        
        auto recovered = result.OrElse([&](const ScriptError& e) {
            return engine.ExecuteSync("'recovered'");
        });
        
        REQUIRE(recovered.IsOk());
        CHECK(recovered.ToString() == "recovered");
    }
    
    SECTION("35.6 OrElse passthrough on success") {
        auto result = engine.ExecuteSync("'original'");
        
        bool called = false;
        auto same = result.OrElse([&](const ScriptError& e) {
            called = true;
            return engine.ExecuteSync("'fallback'");
        });
        
        CHECK_FALSE(called);
        CHECK(same.ToString() == "original");
    }
    
    SECTION("35.7 Match on success") {
        auto result = engine.ExecuteSync("100");
        
        std::string matched = result.Match(
            [](const ScriptValue& v) { return "ok:" + v.ToString(); },
            [](const ScriptError& e) { return "err:" + e.message; }
        );
        
        CHECK(matched == "ok:100");
    }
    
    SECTION("35.8 Match on error") {
        auto result = engine.ExecuteSync("throw new Error('fail')");
        
        std::string matched = result.Match(
            [](const ScriptValue& v) { return "ok"; },
            [](const ScriptError& e) { return "err"; }
        );
        
        CHECK(matched == "err");
    }
    
    SECTION("35.9 Inspect for side effects") {
        auto result = engine.ExecuteSync("'inspected'");
        
        std::string captured;
        result.Inspect([&](const ScriptValue& v) {
            captured = v.ToString();
        });
        
        CHECK(captured == "inspected");
    }
    
    SECTION("35.10 InspectError for error side effects") {
        auto result = engine.ExecuteSync("throw new Error('logged')");
        
        std::string captured;
        result.InspectError([&](const ScriptError& e) {
            captured = e.message;
        });
        
        CHECK(captured.find("logged") != std::string::npos);
    }
    
    SECTION("35.11 Method chaining") {
        // Demonstrate fluent API with chaining
        std::string log;
        
        auto result = engine.ExecuteSync("50")
            .Inspect([&](const ScriptValue& v) { log += "got:" + v.ToString() + ";"; })
            .Transform([](const ScriptValue& v) { return v; })  // identity
            .Inspect([&](const ScriptValue& v) { log += "done;"; });
        
        CHECK(result.IsOk());
        CHECK(log == "got:50;done;");
    }
}

//=============================================================================
// Test 36: V8 Locker/Unlocker Mechanism and Nested Value Access
//=============================================================================

TEST_CASE("Test 36: V8 Locker/Unlocker and Nested Access", "[script][locker][value]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("36.1 Sequential execution and value access") {
        auto r1 = engine.ExecuteSync("42");
        REQUIRE(r1.IsOk());
        CHECK(r1.ToNumber().value_or(0) == 42);
        
        auto r2 = engine.ExecuteSync("'hello'");
        REQUIRE(r2.IsOk());
        CHECK(r2.ToString() == "hello");
        
        // First result still valid
        CHECK(r1.Value().IsNumber());
    }
    
    SECTION("36.2 Function exec then Call") {
        auto funcRes = engine.ExecuteSync("(function(x) { return x * 2; })");
        REQUIRE(funcRes.IsOk());
        REQUIRE(funcRes.Value().IsFunction());
        
        auto argRes = engine.ExecuteSync("21");
        REQUIRE(argRes.IsOk());
        
        std::vector<ScriptValue*> args = {&argRes.Value()};
        auto callRes = funcRes.Value().Call(args);
        REQUIRE(callRes.HasValue());
        CHECK(callRes.ToNumber().value_or(0) == 42);
    }
    
    SECTION("36.3 Object then CallMethod") {
        auto objRes = engine.ExecuteSync(R"(
            ({ value: 10, multiply: function(x) { return this.value * x; } })
        )");
        REQUIRE(objRes.IsOk());
        
        auto argRes = engine.ExecuteSync("5");
        std::vector<ScriptValue*> args = {&argRes.Value()};
        
        auto result = objRes.Value().CallMethod("multiply", args);
        REQUIRE(result.HasValue());
        CHECK(result.ToNumber().value_or(0) == 50);
    }
    
    SECTION("36.4 Nested property chain") {
        auto res = engine.ExecuteSync("({ a: { b: { c: { d: 'deep' } } } })");
        REQUIRE(res.IsOk());
        
        auto d = res.Value().Get("a").Get("b").Get("c").Get("d");
        REQUIRE(d.HasValue());
        CHECK(d.ToString() == "deep");
    }
    
    SECTION("36.5 Interleaved exec and access") {
        auto r1 = engine.ExecuteSync("({ n: 1 })");
        auto r2 = engine.ExecuteSync("({ n: 2 })");
        
        CHECK(r1.Value().Get("n").ToNumber().value_or(0) == 1);
        
        auto r3 = engine.ExecuteSync("({ n: 3 })");
        
        CHECK(r2.Value().Get("n").ToNumber().value_or(0) == 2);
        CHECK(r3.Value().Get("n").ToNumber().value_or(0) == 3);
        CHECK(r1.Value().Get("n").ToNumber().value_or(0) == 1);
    }
    
    SECTION("36.6 Currying (function returning function)") {
        auto res = engine.ExecuteSync("(a => b => a + b)");
        REQUIRE(res.IsOk());
        
        auto arg10 = engine.ExecuteSync("10");
        std::vector<ScriptValue*> a1 = {&arg10.Value()};
        auto inner = res.Value().Call(a1);
        REQUIRE(inner.HasValue());
        REQUIRE(inner.IsFunction());
        
        auto arg5 = engine.ExecuteSync("5");
        std::vector<ScriptValue*> a2 = {&arg5.Value()};
        auto final = inner.Call(a2);
        CHECK(final.ToNumber().value_or(0) == 15);
    }
    
    SECTION("36.7 Array iteration") {
        auto res = engine.ExecuteSync("[10, 20, 30, 40, 50]");
        REQUIRE(res.IsOk());
        
        auto len = res.Value().Length();
        REQUIRE(len.value_or(0) == 5);
        
        double sum = 0;
        for (uint32_t i = 0; i < *len; i++) {
            sum += res.Value().Get(i).ToNumber().value_or(0);
        }
        CHECK(sum == 150);
    }
    
    SECTION("36.8 Method chaining on returned objects") {
        auto res = engine.ExecuteSync(R"(
            ({ v: 1, add: function(x) { return { v: this.v + x, add: this.add, mul: this.mul }; },
                      mul: function(x) { return { v: this.v * x, add: this.add, mul: this.mul }; } })
        )");
        REQUIRE(res.IsOk());
        
        auto a5 = engine.ExecuteSync("5");
        auto a2 = engine.ExecuteSync("2");
        auto a3 = engine.ExecuteSync("3");
        
        std::vector<ScriptValue*> v5 = {&a5.Value()};
        std::vector<ScriptValue*> v2 = {&a2.Value()};
        std::vector<ScriptValue*> v3 = {&a3.Value()};
        
        // Chain: 1 + 5 = 6, * 2 = 12, + 3 = 15
        auto s1 = res.Value().CallMethod("add", v5);
        auto s2 = s1.CallMethod("mul", v2);
        auto s3 = s2.CallMethod("add", v3);
        
        CHECK(s3.Get("v").ToNumber().value_or(0) == 15);
    }
    
    SECTION("36.9 All value types in sequence") {
        auto num = engine.ExecuteSync("123");
        auto str = engine.ExecuteSync("'test'");
        auto boolV = engine.ExecuteSync("true");
        auto obj = engine.ExecuteSync("({})");
        auto arr = engine.ExecuteSync("[]");
        auto fn = engine.ExecuteSync("(function(){})");
        auto null = engine.ExecuteSync("null");
        auto undef = engine.ExecuteSync("undefined");
        
        CHECK(num.Value().IsNumber());
        CHECK(str.Value().IsString());
        CHECK(boolV.Value().IsBoolean());
        CHECK(obj.Value().IsObject());
        CHECK(arr.Value().IsArray());
        CHECK(fn.Value().IsFunction());
        CHECK(null.Value().IsNull());
        CHECK(undef.Value().IsUndefined());
    }
    
    SECTION("36.10 Deep nesting with mixed operations") {
        auto res = engine.ExecuteSync(R"(
            ({ calc: { compute: function(a, b) {
                return { sum: a + b, prod: a * b };
            }}})
        )");
        REQUIRE(res.IsOk());
        
        auto a7 = engine.ExecuteSync("7");
        auto a6 = engine.ExecuteSync("6");
        std::vector<ScriptValue*> args = {&a7.Value(), &a6.Value()};
        
        auto calc = res.Value().Get("calc");
        auto computed = calc.CallMethod("compute", args);
        
        CHECK(computed.Get("sum").ToNumber().value_or(0) == 13);
        CHECK(computed.Get("prod").ToNumber().value_or(0) == 42);
    }
}

//=============================================================================
// Test 37: New API Features
//=============================================================================

TEST_CASE("Test 37: New API Features", "[script][api]") {
    auto& engine = ScriptEngine::Instance();
    
    SECTION("37.1 operator[] for property access") {
        auto res = engine.ExecuteSync("({ a: { b: { c: 42 } } })");
        REQUIRE(res.IsOk());
        
        // Use operator[] instead of Get()
        auto val = res.Value()["a"]["b"]["c"];
        REQUIRE(val.HasValue());
        CHECK(val.ToNumber().value_or(0) == 42);
    }
    
    SECTION("37.2 operator[] for array access") {
        auto res = engine.ExecuteSync("[10, 20, 30]");
        REQUIRE(res.IsOk());
        
        CHECK(res.Value()[0].ToNumber().value_or(0) == 10);
        CHECK(res.Value()[1].ToNumber().value_or(0) == 20);
        CHECK(res.Value()[2].ToNumber().value_or(0) == 30);
    }
    
    SECTION("37.3 As<T>() template extraction") {
        auto res = engine.ExecuteSync("({ num: 42.5, str: 'hello', flag: true })");
        REQUIRE(res.IsOk());
        
        auto num = res.Value()["num"].As<double>();
        auto intNum = res.Value()["num"].As<int>();
        auto str = res.Value()["str"].As<std::string>();
        auto flag = res.Value()["flag"].As<bool>();
        
        CHECK(num.value_or(0) == 42.5);
        CHECK(intNum.value_or(0) == 42);
        CHECK(str.value_or("") == "hello");
        CHECK(flag.value_or(false) == true);
    }
    
    SECTION("37.4 Array iteration with range-for") {
        auto res = engine.ExecuteSync("[1, 2, 3, 4, 5]");
        REQUIRE(res.IsOk());
        
        double sum = 0;
        for (auto elem : res.Value()) {
            sum += elem.ToNumber().value_or(0);
        }
        CHECK(sum == 15);
    }
    
    SECTION("37.5 ExecuteSyncNumber") {
        auto num = engine.ExecuteSyncNumber("100 + 23");
        REQUIRE(num.has_value());
        CHECK(*num == 123);
        
        // Returns nullopt on error
        auto err = engine.ExecuteSyncNumber("throw 'err'");
        CHECK_FALSE(err.has_value());
    }
    
    SECTION("37.6 ExecuteSyncString") {
        auto str = engine.ExecuteSyncString("'hello' + ' world'");
        REQUIRE(str.has_value());
        CHECK(*str == "hello world");
    }
    
    SECTION("37.7 ExecuteSyncBool") {
        auto t = engine.ExecuteSyncBool("5 > 3");
        auto f = engine.ExecuteSyncBool("5 < 3");
        
        REQUIRE(t.has_value());
        REQUIRE(f.has_value());
        CHECK(*t == true);
        CHECK(*f == false);
    }
    
    SECTION("37.8 ExecuteAsync") {
        auto future = engine.ExecuteAsync("41 + 1");
        
        // Get result (blocks until complete)
        auto result = future.get();
        
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 42);
    }
    
    SECTION("37.9 Script::Builder pattern") {
        auto script = Script::Builder()
            .Code("'built with builder'")
            .Name("builder-test")
            .Priority(ScriptPriority::High)
            .Timeout(5s)
            .Trusted(true)
            .Author("test-author")
            .Build();
        
        REQUIRE(script);
        CHECK(script->GetName() == "builder-test");
        CHECK(script->GetPriority() == ScriptPriority::High);
        CHECK(script->GetTimeout() == 5s);
        CHECK(script->IsTrusted() == true);
    }
    
    SECTION("37.10 ToJsonString") {
        auto obj = engine.ExecuteSync("({ x: 1, y: 'test', z: [1, 2, 3] })");
        REQUIRE(obj.IsOk());
        
        std::string json = obj.Value().ToJsonString();
        
        // Should contain JSON representation
        CHECK(json.find("\"x\":1") != std::string::npos);
        CHECK(json.find("\"y\":\"test\"") != std::string::npos);
        CHECK(json.find("[1,2,3]") != std::string::npos);
    }
    
    SECTION("37.11 ToJsonString primitives") {
        CHECK(engine.ExecuteSync("42").Value().ToJsonString() == "42");
        CHECK(engine.ExecuteSync("'hello'").Value().ToJsonString() == "\"hello\"");
        CHECK(engine.ExecuteSync("true").Value().ToJsonString() == "true");
        CHECK(engine.ExecuteSync("null").Value().ToJsonString() == "null");
    }
    
    SECTION("37.12 Combined API usage") {
        // Complex scenario using multiple new features
        auto res = engine.ExecuteSync(R"(
            ({ items: [{ id: 1, name: 'one' }, { id: 2, name: 'two' }] })
        )");
        REQUIRE(res.IsOk());
        
        std::string names;
        for (auto item : res.Value()["items"]) {
            auto id = item["id"].As<int>().value_or(0);
            auto name = item["name"].As<std::string>().value_or("");
            names += std::to_string(id) + ":" + name + ";";
        }
        
        CHECK(names == "1:one;2:two;");
    }
}

//=============================================================================
// Test 38: Directive Hooks
//=============================================================================

TEST_CASE("Test 38: Directive Hooks", "[script][directive]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();
    REQUIRE(env);
    
    SECTION("38.1 Register and trigger directive") {
        bool handler_called = false;
        std::string received_directive;
        
        env->RegisterDirective("test", [&](ScriptEnvironment* e, const std::string& directive) {
            handler_called = true;
            received_directive = directive;
        });
        
        // Execute script with directive
        auto result = env->ExecuteSync(R"("use test"; 42)", 2s);
        
        CHECK(handler_called);
        CHECK(received_directive == "test");
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 42);
        
        // Cleanup
        env->UnregisterDirective("test");
    }
    
    SECTION("38.2 Multiple directives") {
        int count = 0;
        
        env->RegisterDirective("first", [&](auto*, auto&) { count += 1; });
        env->RegisterDirective("second", [&](auto*, auto&) { count += 10; });
        
        auto result = env->ExecuteSync(R"("use first"; "use second"; 1)", 2s);
        
        CHECK(count == 11);
        REQUIRE(result.IsOk());
        
        env->UnregisterDirective("first");
        env->UnregisterDirective("second");
    }
    
    SECTION("38.3 Unregistered directive is ignored") {
        auto result = env->ExecuteSync(R"("use nonexistent"; 123)", 2s);
        
        // Should still execute successfully
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 123);
    }
    
    SECTION("38.4 Single quotes work too") {
        bool called = false;
        
        env->RegisterDirective("singlequote", [&](auto*, auto&) { called = true; });
        
        auto result = env->ExecuteSync("'use singlequote'; 99", 2s);
        
        CHECK(called);
        REQUIRE(result.IsOk());
        
        env->UnregisterDirective("singlequote");
    }
    
    SECTION("38.5 Directive only at start of script") {
        int count = 0;
        
        env->RegisterDirective("middle", [&](auto*, auto&) { count++; });
        
        // Directive in middle of code should NOT trigger (it's after a statement)
        auto result = env->ExecuteSync(R"(
            1 + 1;
            "use middle";
            2 + 2
        )", 2s);

        CHECK(result);
        CHECK(count == 0);  // Not triggered - not at start
        // Even if script runs, the directive shouldn't have triggered
        
        env->UnregisterDirective("middle");
    }
}

//=============================================================================
// Test 39: Advanced Features (Phase 1-4)
//=============================================================================

TEST_CASE("Test 39: Advanced Features", "[script][advanced]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();
    REQUIRE(env);
    
    SECTION("39.1 ToVector<T> type conversion") {
        auto res = engine.ExecuteSync("[1, 2, 3, 4, 5]");
        REQUIRE(res.IsOk());
        
        auto vec = res.Value().ToVector<double>();
        REQUIRE(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[4] == 5);
        
        // String vector
        auto strRes = engine.ExecuteSync("['a', 'b', 'c']");
        REQUIRE(strRes.IsOk());
        auto strVec = strRes.Value().ToVector<std::string>();
        REQUIRE(strVec.size() == 3);
        CHECK(strVec[0] == "a");
    }
    
    SECTION("39.2 Keys() for objects") {
        auto res = engine.ExecuteSync("({ foo: 1, bar: 2, baz: 3 })");
        REQUIRE(res.IsOk());
        
        auto keys = res.Value().Keys();
        CHECK(keys.size() == 3);
        // Keys may be in any order
        CHECK(std::find(keys.begin(), keys.end(), "foo") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "bar") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "baz") != keys.end());
    }
    
    SECTION("39.3 SetGlobal and GetGlobal") {
        env->SetGlobal("testNumber", 42.0);
        env->SetGlobal("testString", "hello");
        env->SetGlobal("testBool", true);
        
        auto numRes = engine.ExecuteSync("testNumber");
        auto strRes = engine.ExecuteSync("testString");
        auto boolRes = engine.ExecuteSync("testBool");
        
        REQUIRE(numRes.IsOk());
        REQUIRE(strRes.IsOk());
        REQUIRE(boolRes.IsOk());
        
        CHECK(numRes.ToNumber().value_or(0) == 42);
        CHECK(strRes.ToString() == "hello");
        CHECK(boolRes.ToBool().value_or(false) == true);
    }
    
    SECTION("39.4 Compile and run cached script") {
        auto compiled = env->Compile("40 + 2", "test-compiled");
        REQUIRE(compiled);
        CHECK(compiled->GetName() == "test-compiled");
        CHECK(compiled->IsValid());
        
        auto result = compiled->Run();
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 42);
        
        // Can run multiple times
        auto result2 = compiled->Run();
        REQUIRE(result2.IsOk());
        CHECK(result2.ToNumber().value_or(0) == 42);
    }
    
    SECTION("39.5 IsolationLevel and CanAccess") {
        // Default is Full
        CHECK(env->GetIsolationLevel() == ScriptEnvironment::IsolationLevel::Full);
        CHECK(env->CanAccess("fs") == true);
        CHECK(env->CanAccess("anything") == true);
        
        // Restricted blocks fs, net, etc
        env->SetIsolationLevel(ScriptEnvironment::IsolationLevel::Restricted);
        CHECK(env->CanAccess("fs") == false);
        CHECK(env->CanAccess("net") == false);
        CHECK(env->CanAccess("math") == true);
        
        // Minimal allows only basic compute
        env->SetIsolationLevel(ScriptEnvironment::IsolationLevel::Minimal);
        CHECK(env->CanAccess("math") == true);
        CHECK(env->CanAccess("json") == true);
        CHECK(env->CanAccess("fs") == false);
        CHECK(env->CanAccess("timers") == false);
        
        // Reset to Full
        env->SetIsolationLevel(ScriptEnvironment::IsolationLevel::Full);
    }
    
    SECTION("39.6 ExecuteSyncAwait with immediate value") {
        auto result = env->ExecuteSyncAwait("42", 2s);
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 42);
    }
    
    SECTION("39.7 ExecuteSyncAwait with resolved promise") {
        auto result = env->ExecuteSyncAwait("Promise.resolve(123)", 2s);
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 123);
    }
    
    SECTION("39.8 RegisterModule and RequireModule") {
        env->RegisterModule("myutils", R"(
            module.exports = {
                add: function(a, b) { return a + b; },
                mul: function(a, b) { return a * b; }
            };
        )");
        
        auto exports = env->RequireModule("myutils");
        REQUIRE(exports.IsOk());
        
        // Test that exports has the functions
        auto addFn = exports.Value().Get("add");
        CHECK(addFn.IsFunction());
        
        env->UnregisterModule("myutils");
    }
    
    SECTION("39.9 RequireModule not found") {
        auto result = env->RequireModule("nonexistent");
        CHECK(result.IsError());
    }
    
    SECTION("39.10 WatchModule and ReloadModule") {
        bool reloadCalled = false;
        std::string reloadedName;
        
        env->WatchModule("watchtest", [&](const std::string& name) {
            reloadCalled = true;
            reloadedName = name;
        });
        
        env->ReloadModule("watchtest");
        
        CHECK(reloadCalled);
        CHECK(reloadedName == "watchtest");
        
        env->UnwatchModule("watchtest");
        
        // After unwatch, reload shouldn't trigger callback
        reloadCalled = false;
        env->ReloadModule("watchtest");
        CHECK_FALSE(reloadCalled);
    }
    
    SECTION("39.11 CreateArray and CreateObject") {
        auto arr = ScriptValue::CreateArray(env.get(), 3);
        CHECK(arr.HasValue());
        CHECK(arr.IsArray());
        
        auto obj = ScriptValue::CreateObject(env.get());
        CHECK(obj.HasValue());
        CHECK(obj.IsObject());
    }
    
    SECTION("39.12 GetModule") {
        env->RegisterModule("gettest", "var x = 42;");
        
        auto code = env->GetModule("gettest");
        CHECK(code == "var x = 42;");
        
        auto missing = env->GetModule("missing");
        CHECK(missing.empty());
        
        env->UnregisterModule("gettest");
    }
}

//=============================================================================
// Test 40: Sandbox Context (Multi-Context Sandboxing)
//=============================================================================

TEST_CASE("Test 40: Sandbox Context", "[script][sandbox]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();
    REQUIRE(env);
    
    SECTION("40.1 Create empty sandbox") {
        auto sandbox = env->CreateSandbox("test-sandbox");
        REQUIRE(sandbox);
        CHECK(sandbox->GetName() == "test-sandbox");
        CHECK(sandbox->IsValid());
    }
    
    SECTION("40.2 Sandbox has isolated globals") {
        // Set a global in main context
        env->SetGlobal("mainVar", 999.0);
        
        // Create sandbox without that global
        auto sandbox = env->CreateSandbox();
        REQUIRE(sandbox);
        
        // mainVar should not exist in sandbox
        auto result = sandbox->Run("typeof mainVar");
        REQUIRE(result.IsOk());
        CHECK(result.ToString() == "undefined");
        
        // Set sandbox-local global
        sandbox->SetGlobal("sandboxVar", 42.0);
        
        auto result2 = sandbox->Run("sandboxVar");
        REQUIRE(result2.IsOk());
        CHECK(result2.ToNumber().value_or(0) == 42);
    }
    
    SECTION("40.3 Contextify - share object from main context") {
        // Create object in main context
        auto objResult = engine.ExecuteSync("({ count: 0, inc: function() { this.count++; } })");
        REQUIRE(objResult.IsOk());
        
        // Create sandbox with that object as shared global
        std::map<std::string, ScriptValue> sandbox_values;
        sandbox_values["shared"] = objResult.Value();
        
        auto sandbox = env->CreateSandbox(sandbox_values, "contextify-test");
        REQUIRE(sandbox);
        
        // Modify in sandbox
        auto result = sandbox->Run("shared.inc(); shared.count");
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(-1) == 1);
    }
    
    SECTION("40.4 Multiple sandboxes are independent") {
        auto sandbox1 = env->CreateSandbox("sandbox1");
        auto sandbox2 = env->CreateSandbox("sandbox2");
        REQUIRE(sandbox1);
        REQUIRE(sandbox2);
        
        sandbox1->SetGlobal("x", 10.0);
        sandbox2->SetGlobal("x", 20.0);
        
        auto res1 = sandbox1->Run("x");
        auto res2 = sandbox2->Run("x");
        
        REQUIRE(res1.IsOk());
        REQUIRE(res2.IsOk());
        CHECK(res1.ToNumber().value_or(0) == 10);
        CHECK(res2.ToNumber().value_or(0) == 20);
    }
    
    SECTION("40.5 Sandbox SetGlobal/GetGlobal") {
        auto sandbox = env->CreateSandbox();
        REQUIRE(sandbox);
        
        sandbox->SetGlobal("num", 123.0);
        sandbox->SetGlobal("str", "hello");
        sandbox->SetGlobal("flag", true);
        
        auto num = sandbox->GetGlobal("num");
        auto str = sandbox->GetGlobal("str");
        auto flag = sandbox->GetGlobal("flag");
        
        CHECK(num.ToNumber().value_or(0) == 123);
        CHECK(str.ToString() == "hello");
        CHECK(flag.ToBool().value_or(false) == true);
    }
    
    SECTION("40.6 Sandbox compile error handling") {
        auto sandbox = env->CreateSandbox();
        REQUIRE(sandbox);
        
        auto result = sandbox->Run("invalid syntax here {{{");
        CHECK(result.IsError());
    }
    
    SECTION("40.7 Run same compiled logic in different contexts") {
        auto sandbox1 = env->CreateSandbox();
        auto sandbox2 = env->CreateSandbox();
        REQUIRE(sandbox1);
        REQUIRE(sandbox2);
        
        sandbox1->SetGlobal("multiplier", 2.0);
        sandbox2->SetGlobal("multiplier", 10.0);
        
        std::string code = "10 * multiplier";
        
        auto res1 = sandbox1->Run(code);
        auto res2 = sandbox2->Run(code);
        
        REQUIRE(res1.IsOk());
        REQUIRE(res2.IsOk());
        CHECK(res1.ToNumber().value_or(0) == 20);
        CHECK(res2.ToNumber().value_or(0) == 100);
    }
}

//=============================================================================
// Test 41: V8 Compile Callbacks (Bytecode Caching)
//=============================================================================

TEST_CASE("Test 41: V8 Compile Callbacks", "[script][compile]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();
    REQUIRE(env);
    
    SECTION("41.1 Compile and get source") {
        auto compiled = env->Compile("1 + 2", "test-source");
        REQUIRE(compiled);
        CHECK(compiled->GetSource() == "1 + 2");
    }
    
    SECTION("41.2 GetCachedData returns bytecode") {
        auto compiled = env->Compile("function add(a,b) { return a + b; } add(1,2)", "cache-test");
        REQUIRE(compiled);
        
        auto cache = compiled->GetCachedData();
        
        // Cache should contain bytecode
        CHECK(!cache.empty());
        CHECK(cache.size() > 100);  // Bytecode should be substantial
    }
    
    SECTION("41.3 FromCachedData restores compiled script") {
        std::string code = "(function() { var x = 10; return x * 5; })()";
        
        // First - compile and cache
        auto original = env->Compile(code, "original");
        REQUIRE(original);
        
        auto cache = original->GetCachedData();
        REQUIRE(!cache.empty());
        
        // Second - restore from cache
        auto restored = CompiledScript::FromCachedData(env.get(), cache, code, "restored");
        REQUIRE(restored);
        CHECK(restored->GetName() == "restored");
        CHECK(restored->IsValid());
        
        // Should produce same result
        auto result = restored->Run();
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 50);
    }
    
    SECTION("41.4 CompileFromCache convenience") {
        std::string code = "42 * 2";
        
        auto original = env->Compile(code, "conv-test");
        auto cache = original->GetCachedData();
        
        auto restored = env->CompileFromCache(cache, code, "conv-restored");
        REQUIRE(restored);
        
        auto result = restored->Run();
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 84);
    }
    
    SECTION("41.5 WasCacheRejected false for valid cache") {
        std::string code = "100 + 23";
        
        auto original = env->Compile(code);
        auto cache = original->GetCachedData();
        
        auto restored = env->CompileFromCache(cache, code);
        REQUIRE(restored);
        CHECK_FALSE(restored->WasCacheRejected());
    }
    
    SECTION("41.6 CompileFunction with parameters") {
        // Create argument values
        auto aRes = engine.ExecuteSync("5");
        auto bRes = engine.ExecuteSync("3");
        REQUIRE(aRes.IsOk());
        REQUIRE(bRes.IsOk());
        
        std::vector<std::string> params = {"a", "b"};
        std::vector args = {aRes.Value(), bRes.Value()};
        
        auto result = env->CompileFunction("return a + b", params, args);
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 8);
    }
    
    SECTION("41.7 CompileFunction with no parameters") {
        std::vector<std::string> params = {};
        std::vector<ScriptValue> args = {};
        
        auto result = env->CompileFunction("return 42", params, args);
        REQUIRE(result.IsOk());
        CHECK(result.ToNumber().value_or(0) == 42);
    }
    
    SECTION("41.8 CompileFunction parameter mismatch error") {
        std::vector<std::string> params = {"a", "b"};
        std::vector<ScriptValue> args = {};  // Wrong count
        
        auto result = env->CompileFunction("return a + b", params, args);
        CHECK(result.IsError());
    }
}

//=============================================================================
// Test 42: V8 Value Debug Logger
//=============================================================================

TEST_CASE("Test 42: V8ValueToDebugString", "[script][logger]") {
    auto& engine = ScriptEngine::Instance();
    auto env = engine.GetMainEnvironment();
    REQUIRE(env);
    
    SECTION("42.1 Log number value") {
        auto result = engine.ExecuteSync("42");
        REQUIRE(result.IsOk());
        // Verify ToString works on numbers
        CHECK(result.ToString() == "42");
    }
    
    SECTION("42.2 Log string value") {
        auto result = engine.ExecuteSync("'test string'");
        REQUIRE(result.IsOk());
        CHECK(result.ToString() == "test string");
    }
    
    SECTION("42.3 Log boolean value") {
        auto result = engine.ExecuteSync("true");
        REQUIRE(result.IsOk());
        CHECK(result.ToBool().value_or(false) == true);
    }
    
    SECTION("42.4 Log object value") {
        auto result = engine.ExecuteSync("({a: 1, b: 2})");
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        CHECK(val.IsObject());
    }
    
    SECTION("42.5 Log array value") {
        auto result = engine.ExecuteSync("[1, 2, 3]");
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        CHECK(val.IsArray());
    }
    
    SECTION("42.6 Log undefined value") {
        auto result = engine.ExecuteSync("undefined");
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        CHECK(val.IsUndefined());
    }
    
    SECTION("42.7 Log null value") {
        auto result = engine.ExecuteSync("null");
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        CHECK(val.IsNull());
    }
    
    SECTION("42.8 Log function value") {
        auto result = engine.ExecuteSync("(function() { return 42; })");
        REQUIRE(result.IsOk());
        auto& val = result.Value();
        CHECK(val.IsFunction());
    }
}

int main(int argc, char* argv[]) {
    auto& engine = ScriptEngine::Instance();
    // Set log level to Error to keep test output clean (suppresses warnings)
    engine.SetLogLevel(LogLevel::Error);
    
    if (!engine.Initialize()) {
        std::cerr << "FATAL: Failed to initialize ScriptEngine" << std::endl;
        return 1;
    }

    int result = Catch::Session().run(argc, argv);

    engine.Shutdown();

    return result;
}
