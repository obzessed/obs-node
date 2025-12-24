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

// all out experimentation sources
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
    CHECK(result.Value() == "42");
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

        CHECK(env->ExecuteSync("API_VERSION;", 2s).Value() == "2.0");
        CHECK(env->ExecuteSync("DEBUG;", 2s).Value() == "true");
    }

    SECTION("6.2 Isolation") {
        EnvironmentConfig c1, c2;
        c1.name = "Iso1"; c1.bootstrap_script = "globalThis.id='1';";
        c2.name = "Iso2"; c2.bootstrap_script = "globalThis.id='2';";

        auto env1 = engine.CreateEnvironment(c1);
        auto env2 = engine.CreateEnvironment(c2);

        CHECK(env1->ExecuteSync("id;", 1s).Value() == "1");
        CHECK(env2->ExecuteSync("id;", 1s).Value() == "2");
    }

    SECTION("6.3 Multi-script Execution") {
        EnvironmentConfig config;
        config.name = "Multi-Script";
        config.bootstrap_script = "globalThis.cnt = 0;";

        auto env = engine.CreateEnvironment(config);

        env->ExecuteSync("cnt++;", 1s);
        env->ExecuteSync("cnt++;", 1s);
        env->ExecuteSync("cnt++;", 1s);

        CHECK(env->ExecuteSync("cnt;", 1s).Value() == "3");
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

int main(int argc, char* argv[]) {
    auto& engine = experiments::ScriptEngine::Instance();
    // Set log level to Warn to keep test output clean, but allow errors
    engine.SetLogLevel(experiments::LogLevel::Warn);
    
    if (!engine.Initialize()) {
        std::cerr << "FATAL: Failed to initialize ScriptEngine" << std::endl;
        return 1;
    }

    int result = Catch::Session().run(argc, argv);

    engine.Shutdown();

    return result;
}
