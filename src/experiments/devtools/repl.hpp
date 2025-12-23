#pragma once

/**
 * repl.hpp - Interactive Read-Eval-Print Loop
 */

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <chrono>

namespace experiments {

//=============================================================================
// REPL - Interactive Read-Eval-Print Loop
//=============================================================================

struct ReplResult {
    bool success{false};
    std::string value;        // Result value as string
    std::string type;         // Type of the result
    std::string error;        // Error message if failed
    std::chrono::microseconds execution_time{0};

    static ReplResult Success(const std::string& val, const std::string& t = "any") {
        ReplResult r;
        r.success = true;
        r.value = val;
        r.type = t;
        return r;
    }

    static ReplResult Error(const std::string& err) {
        ReplResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

class Repl {
public:
    struct Options {
        std::string prompt{">> "};
        std::string continuation_prompt{".. "};
        size_t max_history{1000};
        bool persist_history{true};
        std::string history_file{".node_repl_history"};
        bool colorize{true};
        bool show_types{false};
        bool strict_mode{false};
    };

    explicit Repl(Options options = {}) : options_(std::move(options)) {}

    // Evaluate a line of code
    ReplResult Evaluate(const std::string& code) {
        auto start = std::chrono::steady_clock::now();

        // Add to history
        AddToHistory(code);

        // Placeholder: In real implementation, would use V8 to evaluate
        ReplResult result;
        result.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);

        // Simple evaluation simulation
        if (code.find("throw") != std::string::npos) {
            result = ReplResult::Error("Simulated error");
        } else if (code.find("undefined") != std::string::npos) {
            result = ReplResult::Success("undefined", "undefined");
        } else if (code.find("null") != std::string::npos) {
            result = ReplResult::Success("null", "null");
        } else if (code.find("true") != std::string::npos || code.find("false") != std::string::npos) {
            result = ReplResult::Success(code.find("true") != std::string::npos ? "true" : "false", "boolean");
        } else if (code.find('+') != std::string::npos || code.find('-') != std::string::npos) {
            result = ReplResult::Success("42", "number");
        } else {
            result = ReplResult::Success("'" + code + "'", "string");
        }

        result.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    // Check if code is complete (for multi-line input)
    bool IsComplete(const std::string& code) const {
        // Count brackets and braces
        int braces = 0, brackets = 0, parens = 0;
        bool in_string = false;
        char string_char = 0;

        for (size_t i = 0; i < code.size(); ++i) {
            char c = code[i];
            if (in_string) {
                if (c == string_char && (i == 0 || code[i-1] != '\\')) {
                    in_string = false;
                }
            } else {
                if (c == '"' || c == '\'' || c == '`') {
                    in_string = true;
                    string_char = c;
                } else if (c == '{') braces++;
                else if (c == '}') braces--;
                else if (c == '[') brackets++;
                else if (c == ']') brackets--;
                else if (c == '(') parens++;
                else if (c == ')') parens--;
            }
        }

        return !in_string && braces == 0 && brackets == 0 && parens == 0;
    }

    // Tab completion
    std::vector<std::string> Complete(const std::string& partial) const {
        std::vector<std::string> completions;

        // Built-in globals
        std::vector<std::string> globals = {
            "console", "setTimeout", "setInterval", "clearTimeout", "clearInterval",
            "Promise", "Array", "Object", "String", "Number", "Boolean", "Date",
            "JSON", "Math", "Error", "RegExp", "Map", "Set", "WeakMap", "WeakSet",
            "Symbol", "Proxy", "Reflect", "Buffer", "process", "require", "module",
            "exports", "global", "globalThis", "__dirname", "__filename"
        };

        // Add context variables
        for (const auto& [name, _] : context_) {
            globals.push_back(name);
        }

        // Filter by prefix
        for (const auto& g : globals) {
            if (g.starts_with(partial)) {
                completions.push_back(g);
            }
        }

        return completions;
    }

    // History navigation
    void AddToHistory(const std::string& line) {
        if (!line.empty() && (history_.empty() || history_.back() != line)) {
            history_.push_back(line);
            if (history_.size() > options_.max_history) {
                history_.erase(history_.begin());
            }
        }
        history_pos_ = history_.size();
    }

    std::optional<std::string> GetPreviousHistory() {
        if (history_pos_ > 0) {
            --history_pos_;
            return history_[history_pos_];
        }
        return std::nullopt;
    }

    std::optional<std::string> GetNextHistory() {
        if (history_pos_ < history_.size() - 1) {
            ++history_pos_;
            return history_[history_pos_];
        }
        history_pos_ = history_.size();
        return std::nullopt;
    }

    // Context management
    void SetContext(const std::string& name, const std::string& value) {
        context_[name] = value;
    }

    std::optional<std::string> GetContext(const std::string& name) const {
        auto it = context_.find(name);
        if (it != context_.end()) return it->second;
        return std::nullopt;
    }

    void ClearContext() { context_.clear(); }

    // Get prompt
    std::string GetPrompt(bool continuation = false) const {
        return continuation ? options_.continuation_prompt : options_.prompt;
    }

    // History access
    const std::vector<std::string>& GetHistory() const { return history_; }
    void ClearHistory() { history_.clear(); history_pos_ = 0; }

    Options& GetOptions() { return options_; }
    
private:
    Options options_;
    std::vector<std::string> history_;
    size_t history_pos_{0};
    std::unordered_map<std::string, std::string> context_;
};

} // namespace experiments
