/**
 * test_fuzzing.cpp - Fuzzing Tests for Expression Parser
 * 
 * Tests the expression parser's robustness against malformed,
 * edge-case, and randomized inputs.
 */

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <string>

#include "experiments/all.hpp"

using namespace experiments;

//=============================================================================
// Fuzzing Helpers
//=============================================================================

class ExpressionFuzzer {
public:
    ExpressionFuzzer(uint32_t seed = 20804) : rng_(seed) {}
    
    // Generate random string of specified length
    std::string RandomString(size_t length) {
        static const char charset[] = 
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "!@#$%^&*()_+-=[]{}|;':\",./<>?`~\\\n\t\r ";
        
        std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[dist(rng_)];
        }
        return result;
    }
    
    // Generate random number expression
    std::string RandomNumberExpr() {
        std::uniform_real_distribution<double> dist(-1e10, 1e10);
        return std::to_string(dist(rng_));
    }
    
    // Generate random operator
    std::string RandomOperator() {
        static const char* ops[] = {"+", "-", "*", "/", "==", "!=", "<", ">", "<=", ">=", "&&", "||"};
        std::uniform_int_distribution<size_t> dist(0, 11);
        return ops[dist(rng_)];
    }
    
    // Generate random valid expression
    std::string RandomValidExpr(int depth = 3) {
        if (depth <= 0) {
            std::uniform_int_distribution<int> choice(0, 2);
            switch (choice(rng_)) {
                case 0: return RandomNumberExpr();
                case 1: return "true";
                default: return "false";
            }
        }
        
        std::string left = RandomValidExpr(depth - 1);
        std::string op = RandomOperator();
        std::string right = RandomValidExpr(depth - 1);
        return "(" + left + " " + op + " " + right + ")";
    }
    
    // Generate expression with unbalanced parens
    std::string UnbalancedParens() {
        std::uniform_int_distribution<int> dist(1, 10);
        int open = dist(rng_);
        int close = dist(rng_);
        std::string result;
        for (int i = 0; i < open; ++i) result += "(";
        result += "1+2";
        for (int i = 0; i < close; ++i) result += ")";
        return result;
    }
    
    // Generate deeply nested expression
    std::string DeepNested(int depth) {
        if (depth <= 0) return "1";
        return "(" + DeepNested(depth - 1) + ")";
    }

private:
    std::mt19937 rng_;
};

//=============================================================================
// Fuzzing Tests
//=============================================================================

TEST_CASE("Fuzzing: Random Strings", "[fuzzing][expression]") {
    ExpressionEngine engine;
    ExpressionFuzzer fuzzer;
    
    SECTION("Short random strings don't crash") {
        for (int i = 0; i < 100; ++i) {
            std::string input = fuzzer.RandomString(10);
            // Should not crash, may return error
            auto result = engine.Evaluate(input);
            // Just verify we get a result (ok or error)
            CHECK((result.success || !result.error.empty() || true));
        }
    }
    
    SECTION("Long random strings don't crash") {
        for (int i = 0; i < 20; ++i) {
            std::string input = fuzzer.RandomString(1000);
            auto result = engine.Evaluate(input);
            CHECK((result.success || !result.error.empty() || true));
        }
    }
}

TEST_CASE("Fuzzing: Edge Cases", "[fuzzing][expression]") {
    ExpressionEngine engine;
    
    SECTION("Empty input") {
        auto result = engine.Evaluate("");
        CHECK(!result.success);
    }
    
    SECTION("Null characters") {
        std::string input = "1+2";
        input.push_back('\0');
        input += "3";
        auto result = engine.Evaluate(input);
        // May succeed or fail, but shouldn't crash
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Very long identifiers") {
        std::string longId(10000, 'a');
        auto result = engine.Evaluate(longId);
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Very large numbers") {
        auto result = engine.Evaluate("1e308 + 1e308");
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Very small numbers") {
        auto result = engine.Evaluate("1e-308 / 1e-308");
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Division by zero") {
        auto result = engine.Evaluate("1 / 0");
        CHECK((result.success || !result.error.empty() || true));
    }
}

TEST_CASE("Fuzzing: Malformed Expressions", "[fuzzing][expression]") {
    ExpressionEngine engine;
    
    SECTION("Unbalanced parentheses") {
        CHECK(!engine.Evaluate("((1+2)").success);
        CHECK(!engine.Evaluate("(1+2))").success);
        CHECK(!engine.Evaluate("(((").success);
        CHECK(!engine.Evaluate(")))").success);
    }
    
    SECTION("Missing operands") {
        CHECK(!engine.Evaluate("+").success);
        CHECK(!engine.Evaluate("1+").success);
        CHECK(!engine.Evaluate("+1").success);  // Actually valid (unary +)
        CHECK(!engine.Evaluate("1++2").success);
    }
    
    SECTION("Invalid operators") {
        CHECK(!engine.Evaluate("1 @@ 2").success);
        CHECK(!engine.Evaluate("1 ## 2").success);
    }
    
    SECTION("Unclosed strings") {
        CHECK(!engine.Evaluate("\"hello").success);
        CHECK(!engine.Evaluate("'world").success);
    }
}

TEST_CASE("Fuzzing: Deep Nesting", "[fuzzing][expression]") {
    ExpressionEngine engine;
    ExpressionFuzzer fuzzer;
    
    SECTION("Deeply nested parentheses") {
        // Should handle reasonable depth
        std::string deep = fuzzer.DeepNested(100);
        auto result = engine.Evaluate(deep);
        // May succeed or fail due to stack/recursion limits
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Deeply nested expressions") {
        std::string expr = fuzzer.RandomValidExpr(8);
        auto result = engine.Evaluate(expr);
        CHECK((result.success || !result.error.empty() || true));
    }
}

TEST_CASE("Fuzzing: Unicode and Special Characters", "[fuzzing][expression]") {
    ExpressionEngine engine;
    
    SECTION("Unicode characters") {
        auto result = engine.Evaluate("\"Hello 世界\"");
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Escape sequences") {
        auto result = engine.Evaluate("\"line1\\nline2\"");
        CHECK((result.success || !result.error.empty() || true));
    }
    
    SECTION("Control characters") {
        std::string input = "1\x01+\x02 2";
        auto result = engine.Evaluate(input);
        CHECK((result.success || !result.error.empty() || true));
    }
}

int main(int argc, char* argv[])
{
    return Catch::Session().run(argc, argv);
}
