#pragma once

/**
 * types.hpp - Expression Value and Context Types
 * 
 * Core types used by the expression engine.
 * Split out to avoid circular dependencies.
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>
#include <variant>
#include <chrono>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace experiments {

//=============================================================================
// Expression Engine - Evaluate formulas and expressions
//=============================================================================

// ExpressionValue - Dynamic value type for expressions
class ExpressionValue {
public:
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    using ArrayType = std::vector<ExpressionValue>;
    using ObjectType = std::unordered_map<std::string, ExpressionValue>;
    using ValueVariant = std::variant<std::nullptr_t, bool, double, std::string, ArrayType, ObjectType>;

    // Constructors
    ExpressionValue() : value_(nullptr), type_(Type::Null) {}
    ExpressionValue(std::nullptr_t) : value_(nullptr), type_(Type::Null) {}
    ExpressionValue(bool v) : value_(v), type_(Type::Boolean) {}
    ExpressionValue(int v) : value_(static_cast<double>(v)), type_(Type::Number) {}
    ExpressionValue(double v) : value_(v), type_(Type::Number) {}
    ExpressionValue(const char* v) : value_(std::string(v)), type_(Type::String) {}
    ExpressionValue(std::string v) : value_(std::move(v)), type_(Type::String) {}
    ExpressionValue(ArrayType v) : value_(std::move(v)), type_(Type::Array) {}
    ExpressionValue(ObjectType v) : value_(std::move(v)), type_(Type::Object) {}

    // Type checking
    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBoolean() const { return type_ == Type::Boolean; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    // Value getters
    bool AsBoolean() const {
        if (type_ == Type::Boolean) return std::get<bool>(value_);
        if (type_ == Type::Number) return std::get<double>(value_) != 0;
        if (type_ == Type::String) return !std::get<std::string>(value_).empty();
        return false;
    }

    double AsNumber() const {
        if (type_ == Type::Number) return std::get<double>(value_);
        if (type_ == Type::Boolean) return std::get<bool>(value_) ? 1.0 : 0.0;
        if (type_ == Type::String) {
            try { return std::stod(std::get<std::string>(value_)); }
            catch (...) { return 0.0; }
        }
        return 0.0;
    }

    std::string AsString() const {
        switch (type_) {
            case Type::Null: return "null";
            case Type::Boolean: return std::get<bool>(value_) ? "true" : "false";
            case Type::Number: {
                double d = std::get<double>(value_);
                if (d == static_cast<int64_t>(d)) return std::to_string(static_cast<int64_t>(d));
                std::ostringstream oss;
                oss << std::setprecision(15) << d;
                return oss.str();
            }
            case Type::String: return std::get<std::string>(value_);
            case Type::Array: return "[Array]";
            case Type::Object: return "[Object]";
        }
        return "";
    }

    const ArrayType& AsArray() const {
        static ArrayType empty;
        return type_ == Type::Array ? std::get<ArrayType>(value_) : empty;
    }

    const ObjectType& AsObject() const {
        static ObjectType empty;
        return type_ == Type::Object ? std::get<ObjectType>(value_) : empty;
    }

    // Operators
    bool operator==(const ExpressionValue& other) const {
        if (type_ != other.type_) return false;
        return value_ == other.value_;
    }

    bool operator!=(const ExpressionValue& other) const {
        return !(*this == other);
    }

    // Serialize to JSON
    std::string ToJson() const {
        switch (type_) {
            case Type::Null: return "null";
            case Type::Boolean: return std::get<bool>(value_) ? "true" : "false";
            case Type::Number: return AsString();
            case Type::String: return "\"" + std::get<std::string>(value_) + "\"";
            case Type::Array: {
                std::ostringstream oss;
                oss << "[";
                const auto& arr = std::get<ArrayType>(value_);
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << arr[i].ToJson();
                }
                oss << "]";
                return oss.str();
            }
            case Type::Object: {
                std::ostringstream oss;
                oss << "{";
                const auto& obj = std::get<ObjectType>(value_);
                bool first = true;
                for (const auto& [k, v] : obj) {
                    if (!first) oss << ",";
                    oss << "\"" << k << "\":" << v.ToJson();
                    first = false;
                }
                oss << "}";
                return oss.str();
            }
        }
        return "null";
    }

    static std::string TypeName(Type t) {
        switch (t) {
            case Type::Null: return "null";
            case Type::Boolean: return "boolean";
            case Type::Number: return "number";
            case Type::String: return "string";
            case Type::Array: return "array";
            case Type::Object: return "object";
        }
        return "unknown";
    }

private:
    ValueVariant value_;
    Type type_;
};

// ExpressionContext - Variable bindings with scope support
class ExpressionContext {
public:
    struct VarInfo {
        ExpressionValue value;
        bool is_const{false};
    };

    using Scope = std::unordered_map<std::string, VarInfo>;

    ExpressionContext() {
        // Create global scope
        scopes_.push_back(Scope());
    }

    explicit ExpressionContext(std::unordered_map<std::string, ExpressionValue> vars) {
        scopes_.push_back(Scope());
        for (auto& [k, v] : vars) {
            scopes_.back()[k] = {std::move(v), false};
        }
    }

    // Set a variable (assignment) - searches up the scope chain
    bool Set(const std::string& name, ExpressionValue value) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto& scope = *it;
            auto var_it = scope.find(name);
            if (var_it != scope.end()) {
                if (var_it->second.is_const) return false; // Cannot assign to const
                var_it->second.value = std::move(value);
                return true;
            }
        }
        // If not found, set in global (top) scope implicitly (loose mode)
        // or fail (strict mode). For now, allow implicit globals in top scope.
        scopes_.front()[name] = {std::move(value), false};
        return true;
    }

    // Declare a variable in current scope
    bool Declare(const std::string& name, ExpressionValue value, bool is_const = false) {
        auto& scope = scopes_.back();
        if (scope.contains(name)) return false; // Already declared in this scope
        scope[name] = {std::move(value), is_const};
        return true;
    }

    // Get a variable
    std::optional<ExpressionValue> Get(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto& scope = *it;
            auto var_it = scope.find(name);
            if (var_it != scope.end()) {
                return var_it->second.value;
            }
        }
        return std::nullopt;
    }

    // Check if variable exists
    bool Has(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (it->contains(name)) return true;
        }
        return false;
    }

    // Enter a new scope
    void EnterScope() {
        scopes_.emplace_back();
    }

    // Exit current scope
    void ExitScope() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
        }
    }

    // Clear all variables (reset to empty global scope)
    void Clear() {
        scopes_.clear();
        scopes_.emplace_back();
    }

    // Get all variable names (visible in current scope)
    std::vector<std::string> GetNames() const {
        std::unordered_set<std::string> unique_names;
        for (const auto& scope : scopes_) {
            for (const auto& [name, _] : scope) {
                unique_names.insert(name);
            }
        }
        return std::vector<std::string>(unique_names.begin(), unique_names.end());
    }

    // Merge with another context (other takes precedence, flattens to global)
    void Merge(const ExpressionContext& other) {
        // Flatten other context into current top scope
        for (const auto& scope : other.scopes_) {
            for (const auto& [name, info] : scope) {
                Set(name, info.value);
            }
        }
    }

    // Create child context (deep copy for now, optimization would be copy-on-write or linked scopes)
    ExpressionContext CreateChild() const {
        ExpressionContext child;
        child.scopes_ = scopes_;
        return child;
    }

    // Convert to JavaScript variable declarations (flattened)
    std::string ToJavaScript() const {
        std::ostringstream oss;
        // Output all variables from all scopes, shadowing behavior handled by order
        // This is a rough approximation
        std::unordered_map<std::string, ExpressionValue> flattened;
        for (const auto& scope : scopes_) {
            for (const auto& [name, info] : scope) {
                flattened[name] = info.value;
            }
        }

        for (const auto& [name, value] : flattened) {
            oss << "const " << name << " = " << value.ToJson() << ";\n";
        }
        return oss.str();
    }

    size_t Size() const {
        size_t count = 0;
        for (const auto& scope : scopes_) count += scope.size();
        return count;
    }

private:
    std::vector<Scope> scopes_;
};

// ExpressionFunction - Callable function for expressions
using ExpressionFunction = std::function<ExpressionValue(const std::vector<ExpressionValue>&)>;

// ExpressionFunctionRegistry - Built-in and custom functions
class ExpressionFunctionRegistry {
public:
    ExpressionFunctionRegistry() {
        RegisterBuiltins();
    }

    // Register a function
    void Register(const std::string& name, ExpressionFunction func,
                  int min_args = 0, int max_args = -1) {
        functions_[name] = {std::move(func), min_args, max_args};
    }

    // Unregister a function
    void Unregister(const std::string& name) {
        functions_.erase(name);
    }

    // Check if function exists
    bool Has(const std::string& name) const {
        return functions_.contains(name);
    }

    // Call a function
    std::optional<ExpressionValue> Call(const std::string& name,
                                         const std::vector<ExpressionValue>& args) const {
        auto it = functions_.find(name);
        if (it == functions_.end()) return std::nullopt;

        const auto& [func, min_args, max_args] = it->second;
        if (static_cast<int>(args.size()) < min_args) return std::nullopt;
        if (max_args >= 0 && static_cast<int>(args.size()) > max_args) return std::nullopt;

        try {
            return func(args);
        } catch (...) {
            return std::nullopt;
        }
    }

    // Get function names
    std::vector<std::string> GetNames() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : functions_) {
            names.push_back(name);
        }
        return names;
    }

    // Generate JavaScript function definitions
    std::string ToJavaScript() const {
        // Built-in functions are available in JS, return empty for now
        return "";
    }

private:
    struct FunctionInfo {
        ExpressionFunction func;
        int min_args;
        int max_args;
    };
    std::unordered_map<std::string, FunctionInfo> functions_;

    void RegisterBuiltins() {
        // Math functions
        Register("abs", [](const auto& args) { return ExpressionValue(std::abs(args[0].AsNumber())); }, 1, 1);
        Register("ceil", [](const auto& args) { return ExpressionValue(std::ceil(args[0].AsNumber())); }, 1, 1);
        Register("floor", [](const auto& args) { return ExpressionValue(std::floor(args[0].AsNumber())); }, 1, 1);
        Register("round", [](const auto& args) { return ExpressionValue(std::round(args[0].AsNumber())); }, 1, 1);
        Register("sqrt", [](const auto& args) { return ExpressionValue(std::sqrt(args[0].AsNumber())); }, 1, 1);
        Register("pow", [](const auto& args) { return ExpressionValue(std::pow(args[0].AsNumber(), args[1].AsNumber())); }, 2, 2);
        Register("min", [](const auto& args) {
            double result = args[0].AsNumber();
            for (size_t i = 1; i < args.size(); ++i) result = std::min(result, args[i].AsNumber());
            return ExpressionValue(result);
        }, 1, -1);
        Register("max", [](const auto& args) {
            double result = args[0].AsNumber();
            for (size_t i = 1; i < args.size(); ++i) result = std::max(result, args[i].AsNumber());
            return ExpressionValue(result);
        }, 1, -1);
        Register("clamp", [](const auto& args) {
            double v = args[0].AsNumber(), lo = args[1].AsNumber(), hi = args[2].AsNumber();
            return ExpressionValue(std::max(lo, std::min(hi, v)));
        }, 3, 3);
        Register("lerp", [](const auto& args) {
            double a = args[0].AsNumber(), b = args[1].AsNumber(), t = args[2].AsNumber();
            return ExpressionValue(a + (b - a) * t);
        }, 3, 3);

        // String functions
        Register("strlen", [](const auto& args) { return ExpressionValue(static_cast<double>(args[0].AsString().length())); }, 1, 1);
        Register("upper", [](const auto& args) {
            std::string s = args[0].AsString();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return ExpressionValue(s);
        }, 1, 1);
        Register("lower", [](const auto& args) {
            std::string s = args[0].AsString();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return ExpressionValue(s);
        }, 1, 1);
        Register("trim", [](const auto& args) {
            std::string s = args[0].AsString();
            s.erase(0, s.find_first_not_of(" \t\n\r"));
            s.erase(s.find_last_not_of(" \t\n\r") + 1);
            return ExpressionValue(s);
        }, 1, 1);
        Register("substr", [](const auto& args) {
            std::string s = args[0].AsString();
            size_t start = static_cast<size_t>(args[1].AsNumber());
            size_t len = args.size() > 2 ? static_cast<size_t>(args[2].AsNumber()) : std::string::npos;
            return ExpressionValue(s.substr(start, len));
        }, 2, 3);
        Register("concat", [](const auto& args) {
            std::string result;
            for (const auto& arg : args) result += arg.AsString();
            return ExpressionValue(result);
        }, 0, -1);

        // Logic functions
        Register("if", [](const auto& args) {
            return args[0].AsBoolean() ? args[1] : (args.size() > 2 ? args[2] : ExpressionValue());
        }, 2, 3);
        Register("and", [](const auto& args) {
            for (const auto& arg : args) if (!arg.AsBoolean()) return ExpressionValue(false);
            return ExpressionValue(true);
        }, 1, -1);
        Register("or", [](const auto& args) {
            for (const auto& arg : args) if (arg.AsBoolean()) return ExpressionValue(true);
            return ExpressionValue(false);
        }, 1, -1);
        Register("not", [](const auto& args) { return ExpressionValue(!args[0].AsBoolean()); }, 1, 1);

        // Type functions
        Register("typeof", [](const auto& args) { return ExpressionValue(ExpressionValue::TypeName(args[0].GetType())); }, 1, 1);
        Register("isNull", [](const auto& args) { return ExpressionValue(args[0].IsNull()); }, 1, 1);
        Register("isNumber", [](const auto& args) { return ExpressionValue(args[0].IsNumber()); }, 1, 1);
        Register("isString", [](const auto& args) { return ExpressionValue(args[0].IsString()); }, 1, 1);
        Register("toNumber", [](const auto& args) { return ExpressionValue(args[0].AsNumber()); }, 1, 1);
        Register("toString", [](const auto& args) { return ExpressionValue(args[0].AsString()); }, 1, 1);
        Register("toBoolean", [](const auto& args) { return ExpressionValue(args[0].AsBoolean()); }, 1, 1);
    }
};

} // namespace experiments
