/**
 * benchmarks.cpp - Performance Benchmarks
 * 
 * Benchmarks for performance-critical paths using Catch2's BENCHMARK.
 */

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <string>
#include <vector>

#include "experiments/all.hpp"

using namespace experiments;
using namespace std::chrono_literals;

//=============================================================================
// Expression Engine Benchmarks
//=============================================================================

TEST_CASE("Benchmark: Expression Evaluation", "[benchmark][expression]") {
    ExpressionEngine engine;
    
    BENCHMARK("Simple arithmetic: 1 + 2") {
        return engine.Evaluate("1 + 2");
    };
    
    BENCHMARK("Complex arithmetic: (1 + 2) * (3 - 4) / 5") {
        return engine.Evaluate("(1 + 2) * (3 - 4) / 5");
    };
    
    BENCHMARK("Boolean logic: true && false || true") {
        return engine.Evaluate("true && false || true");
    };
    
    BENCHMARK("Comparison: 5 > 3 && 10 <= 20") {
        return engine.Evaluate("5 > 3 && 10 <= 20");
    };
    
    BENCHMARK("Nested: ((1 + 2) * (3 + 4)) / ((5 - 6) + 7)") {
        return engine.Evaluate("((1 + 2) * (3 + 4)) / ((5 - 6) + 7)");
    };
    
    BENCHMARK("String: \"hello\" + \" \" + \"world\"") {
        return engine.Evaluate("\"hello\" + \" \" + \"world\"");
    };
}

TEST_CASE("Benchmark: Expression with Context", "[benchmark][expression]") {
    ExpressionEngine engine;
    ExpressionContext ctx;
    ctx.Set("x", ExpressionValue(10.0));
    ctx.Set("y", ExpressionValue(20.0));
    ctx.Set("name", ExpressionValue("test"));
    
    BENCHMARK("Variable lookup: x + y") {
        return engine.Evaluate("x + y", ctx);
    };
    
    BENCHMARK("Variable with arithmetic: (x * 2) + (y / 4)") {
        return engine.Evaluate("(x * 2) + (y / 4)", ctx);
    };
}

TEST_CASE("Benchmark: Expression Cache", "[benchmark][expression]") {
    ExpressionEngine engine;
    engine.GetOptions().cache_enabled = true;
    
    // Prime the cache
    engine.Evaluate("1 + 2 * 3");
    
    BENCHMARK("Cached expression evaluation") {
        return engine.Evaluate("1 + 2 * 3");
    };
}

//=============================================================================
// Module Cache Benchmarks
//=============================================================================

TEST_CASE("Benchmark: Module Cache Operations", "[benchmark][cache]") {
    ModuleCache::Options opts;
    opts.max_entries = 10000;
    ModuleCache cache(opts);
    
    // Pre-populate cache
    for (int i = 0; i < 1000; ++i) {
        ModuleInfo info;
        info.resolved_path = "/path/to/module_" + std::to_string(i) + ".js";
        info.source = "// module content " + std::to_string(i);
        cache.Put(info.resolved_path, info);
    }
    
    BENCHMARK("Cache hit: existing module") {
        return cache.Get("/path/to/module_500.js");
    };
    
    BENCHMARK("Cache miss: non-existent module") {
        return cache.Get("/path/to/nonexistent.js");
    };
    
    BENCHMARK("Cache put: new module") {
        ModuleInfo info;
        info.resolved_path = "/path/to/new_module.js";
        info.source = "// new content";
        cache.Put(info.resolved_path, info);
        return true;
    };
}

//=============================================================================
// Thread-Safe Map Benchmarks
//=============================================================================

TEST_CASE("Benchmark: ThreadSafeMap Operations", "[benchmark][thread]") {
    ThreadSafeMap<std::string, int> map;
    
    // Pre-populate
    for (int i = 0; i < 1000; ++i) {
        map.Set("key_" + std::to_string(i), i);
    }
    
    BENCHMARK("Get existing key") {
        return map.Get("key_500");
    };
    
    BENCHMARK("Get non-existent key") {
        return map.Get("nonexistent");
    };
    
    BENCHMARK("Set key") {
        map.Set("benchmark_key", 42);
        return true;
    };
    
    BENCHMARK("Has key") {
        return map.Has("key_500");
    };
    
    BENCHMARK("Keys()") {
        return map.Keys();
    };
}

//=============================================================================
// Logger Benchmarks
//=============================================================================

TEST_CASE("Benchmark: Logger Formatting", "[benchmark][logger]") {
    LogEntry entry(LogLevel::Info, "benchmark", "Test message for benchmarking");
    
    TextLogFormatter text;
    JsonLogFormatter json;
    
    BENCHMARK("TextLogFormatter::Format") {
        return text.Format(entry);
    };
    
    BENCHMARK("JsonLogFormatter::Format") {
        return json.Format(entry);
    };
}

//=============================================================================
// Result Type Benchmarks
//=============================================================================

TEST_CASE("Benchmark: Result Operations", "[benchmark][result]") {
    Result<int> ok(42);
    Result<int> err(ScriptError::Make(ErrorCode::RuntimeError, "error"));
    
    BENCHMARK("Result::Map (ok)") {
        return ok.Map([](int x) { return x * 2; });
    };
    
    BENCHMARK("Result::Map (error)") {
        return err.Map([](int x) { return x * 2; });
    };
    
    BENCHMARK("Result::FlatMap (ok)") {
        return ok.FlatMap([](int x) -> Result<int> { return x * 2; });
    };
}

//=============================================================================
// ExpressionValue Operators Benchmarks
//=============================================================================

TEST_CASE("Benchmark: ExpressionValue Operators", "[benchmark][expression]") {
    ExpressionValue a(10.0);
    ExpressionValue b(5.0);
    ExpressionValue s1("hello");
    ExpressionValue s2(" world");
    
    BENCHMARK("Numeric addition") {
        return a + b;
    };
    
    BENCHMARK("Numeric multiplication") {
        return a * b;
    };
    
    BENCHMARK("String concatenation") {
        return s1 + s2;
    };
    
    BENCHMARK("Comparison <") {
        return a < b;
    };
    
    BENCHMARK("Logical &&") {
        return ExpressionValue(true) && ExpressionValue(false);
    };
}

//=============================================================================
// Script Execution Benchmarks
//=============================================================================

TEST_CASE("Benchmark: Script Execution", "[benchmark][script]") {
    auto& engine = ScriptEngine::Instance();
    // Ensure engine is initialized (done in main, but safe to check)
    if (!engine.IsInitialized()) {
        SKIP("ScriptEngine not initialized");
    }

    BENCHMARK("ExecuteSync simple string") {
        return engine.ExecuteSync("'hello world'");
    };

    BENCHMARK("ExecuteSync arithmetic") {
        return engine.ExecuteSync("1 + 2 * 3 / 4");
    };

    BENCHMARK("ExecuteSync loop (1000 iter)") {
        return engine.ExecuteSync(
            "var sum = 0;"
            "for (var i = 0; i < 1000; ++i) sum += i;"
            "sum;"
        );
    };

    BENCHMARK("ExecuteSync JSON parse") {
        return engine.ExecuteSync("JSON.parse('{\"a\":1, \"b\":2, \"c\":3}')");
    };

    BENCHMARK("ExecuteSync function call") {
        return engine.ExecuteSync(
            "(function(x) { return x * x; })(10);"
        );
    };
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
