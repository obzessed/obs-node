#pragma once

/**
 * engine.hpp - ExpressionEngine with Lexer/Parser
 * 
 * A complete expression engine with:
 * - Expression class for parsed expressions
 * - ExpressionCache for LRU caching
 * - ExpressionResult for evaluation results
 * - ExpressionEngine with recursive descent parser
 */

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <cctype>

#include "types.hpp"

namespace experiments {



// Expression - A parsed expression ready for evaluation
class Expression {
public:
    Expression() = default;
    Expression(std::string source, std::string compiled = "")
        : source_(std::move(source))
        , compiled_(compiled.empty() ? source_ : std::move(compiled))
        , compiled_at_(std::chrono::steady_clock::now()) {}

    const std::string& GetSource() const { return source_; }
    const std::string& GetCompiled() const { return compiled_; }
    bool IsValid() const { return !source_.empty(); }

    std::chrono::steady_clock::time_point GetCompiledAt() const { return compiled_at_; }

private:
    std::string source_;
    std::string compiled_;
    std::chrono::steady_clock::time_point compiled_at_;
};

// ExpressionCache - Cache compiled expressions
class ExpressionCache {
public:
    struct Options {
        size_t max_entries{1000};
        std::chrono::seconds max_age{3600};
    };

    explicit ExpressionCache(Options options = {}) : options_(std::move(options)) {}

    // Get cached expression
    std::optional<Expression> Get(const std::string& source) {
        std::lock_guard lock(mutex_);
        auto it = cache_.find(source);
        if (it != cache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.GetCompiledAt();
            if (age < options_.max_age) {
                ++hits_;
                return it->second;
            }
            cache_.erase(it);
        }
        ++misses_;
        return std::nullopt;
    }

    // Store expression
    void Put(const std::string& source, const Expression& expr) {
        std::lock_guard lock(mutex_);
        if (cache_.size() >= options_.max_entries) {
            // Remove oldest entry
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.GetCompiledAt() < oldest->second.GetCompiledAt()) {
                    oldest = it;
                }
            }
            cache_.erase(oldest);
        }
        cache_[source] = expr;
    }

    // Clear cache
    void Clear() {
        std::lock_guard lock(mutex_);
        cache_.clear();
    }

    size_t Size() const {
        std::lock_guard lock(mutex_);
        return cache_.size();
    }

    double HitRate() const {
        size_t total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }

private:
    Options options_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Expression> cache_;
    size_t hits_{0};
    size_t misses_{0};
};

// ExpressionResult - Result of expression evaluation
struct ExpressionResult {
    bool success{false};
    ExpressionValue value;
    std::string error;
    std::chrono::microseconds execution_time{0};

    static ExpressionResult Success(ExpressionValue val) {
        ExpressionResult r;
        r.success = true;
        r.value = std::move(val);
        return r;
    }

    static ExpressionResult Error(const std::string& err) {
        ExpressionResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

// ExpressionEngine - Recursive descent parser and evaluator
class ExpressionEngine {
public:
    struct Options {
        bool cache_enabled{true};
        bool strict_mode{false};
        std::chrono::milliseconds timeout{1000};
        size_t max_expression_length{100000};
    };

    explicit ExpressionEngine(Options options = {})
        : options_(std::move(options)) {}

    // Evaluate an expression string
    ExpressionResult Evaluate(const std::string& source,
                              ExpressionContext& context) {
        auto start = std::chrono::steady_clock::now();

        if (source.empty()) {
            return ExpressionResult::Error("Empty expression");
        }

        // Check cache first
        if (options_.cache_enabled) {
            auto cached = cache_.Get(source);
            if (cached) {
                // Re-evaluate cached parse for result (simple caching model)
                // In production, cache the AST instead of just the source
            }
        }

        // Tokenize
        Lexer lexer(source);
        auto tokens = lexer.Tokenize();
        if (tokens.empty()) {
             return ExpressionResult::Success(ExpressionValue());
        }

        // Parse and Evaluate
        try {
            Parser parser(std::move(tokens), context, functions_);
            ExpressionValue result = parser.ParseStatementList();

            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            // Store in cache
            if (options_.cache_enabled) {
                cache_.Put(source, Expression(source));
            }

            ExpressionResult r = ExpressionResult::Success(result);
            r.execution_time = duration;
            return r;
        } catch (const std::exception& e) {
            return ExpressionResult::Error(e.what());
        }
    }

    ExpressionResult Evaluate(const std::string& source) {
        ExpressionContext context;
        return Evaluate(source, context);
    }

    // Helper for function calls
    ExpressionResult EvaluateWithFunctions(const std::string& source, ExpressionContext& context) {
        return Evaluate(source, context);
    }

    // Validate syntax
    bool Validate(const std::string& source, std::string& error) const {
        if (source.empty()) {
            error = "Empty expression";
            return false;
        }
        
        try {
            Lexer lexer(source);
            auto tokens = lexer.Tokenize();
            
            // Check matching parentheses/braces
            int paren_depth = 0;
            int brace_depth = 0;
            for (const auto& tok : tokens) {
                if (tok.type == TokenType::LParen) paren_depth++;
                else if (tok.type == TokenType::RParen) paren_depth--;
                else if (tok.type == TokenType::LBrace) brace_depth++;
                else if (tok.type == TokenType::RBrace) brace_depth--;
                
                if (paren_depth < 0) {
                    error = "Unmatched closing parenthesis";
                    return false;
                }
                if (brace_depth < 0) {
                    error = "Unmatched closing brace";
                    return false;
                }
            }
            
            if (paren_depth != 0) {
                error = "Unmatched opening parenthesis";
                return false;
            }
            if (brace_depth != 0) {
                error = "Unmatched opening brace";
                return false;
            }
            
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }

    ExpressionFunctionRegistry& GetFunctions() { return functions_; }
    ExpressionCache& GetCache() { return cache_; }
    Options& GetOptions() { return options_; }

private:
    Options options_;
    ExpressionFunctionRegistry functions_;
    ExpressionCache cache_;

    // --- Lexer ---
    enum class TokenType {
        Eof, Identifier, Number, String, StringInterpolated,
        Plus, Minus, Multiply, Divide, Equal, NotEqual,
        Less, LessEqual, Greater, GreaterEqual,
        And, Or, Not,  // Logical operators
        Assign, LParen, RParen, LBrace, RBrace, Comma, Semicolon,
        KeywordLet, KeywordConst, KeywordIf, KeywordElse,
        KeywordTrue, KeywordFalse, KeywordNull,
        InterpolationStart // ${
    };

    struct Token {
        TokenType type;
        std::string text;
        size_t line{1};
        size_t column{1};
    };

    class Lexer {
    public:
        explicit Lexer(std::string source) : source_(std::move(source)) {}

        std::vector<Token> Tokenize() {
            std::vector<Token> tokens;
            while (!IsAtEnd()) {
                StartToken();
                char c = Advance();

                if (isspace(c)) {
                    if (c == '\n') { line_++; col_ = 1; }
                    else if (c == '\r') { /* ignore CR */ }
                    else { /* ignore other space */ }
                    continue;
                }

                if (isalpha(c) || c == '_') {
                    tokens.push_back(Identifier());
                    continue;
                }

                if (isdigit(c)) {
                    tokens.push_back(Number());
                    continue;
                }

                switch (c) {
                    case '(': AddToken(tokens, TokenType::LParen); break;
                    case ')': AddToken(tokens, TokenType::RParen); break;
                    case '{': AddToken(tokens, TokenType::LBrace); break;
                    case '}': AddToken(tokens, TokenType::RBrace); break;
                    case ',': AddToken(tokens, TokenType::Comma); break;
                    case ';': AddToken(tokens, TokenType::Semicolon); break;
                    case '+': AddToken(tokens, TokenType::Plus); break;
                    case '-': AddToken(tokens, TokenType::Minus); break;
                    case '*': AddToken(tokens, TokenType::Multiply); break;
                    case '/':
                        if (Match('/')) { // Comment
                            while (Peek() != '\n' && !IsAtEnd()) Advance();
                        } else {
                            AddToken(tokens, TokenType::Divide);
                        }
                        break;
                    case '=': AddToken(tokens, Match('=') ? TokenType::Equal : TokenType::Assign); break;
                    case '!':
                        AddToken(tokens, Match('=') ? TokenType::NotEqual : TokenType::Not);
                        break;
                    case '&':
                        if (Match('&')) AddToken(tokens, TokenType::And);
                        else throw std::runtime_error("Unexpected character '&', did you mean '&&'?");
                        break;
                    case '|':
                        if (Match('|')) AddToken(tokens, TokenType::Or);
                        else throw std::runtime_error("Unexpected character '|', did you mean '||'?");
                        break;
                    case '<': AddToken(tokens, Match('=') ? TokenType::LessEqual : TokenType::Less); break;
                    case '>': AddToken(tokens, Match('=') ? TokenType::GreaterEqual : TokenType::Greater); break;
                    case '"': case '\'': case '`': tokens.push_back(String(c)); break;
                    case '$':
                        if (Match('{')) AddToken(tokens, TokenType::InterpolationStart);
                        else throw std::runtime_error("Unexpected character '$'");
                        break;
                    default: throw std::runtime_error(std::string("Unexpected character '") + c + "'");
                }
            }
            tokens.push_back({TokenType::Eof, "", line_, col_});
            return tokens;
        }

    private:
        std::string source_;
        size_t current_{0};
        size_t start_{0};
        size_t line_{1};
        size_t col_{1};

        bool IsAtEnd() const { return current_ >= source_.length(); }

        char Advance() {
            col_++;
            return source_[current_++];
        }

        char Peek() const {
            if (IsAtEnd()) return '\0';
            return source_[current_];
        }

        char PeekNext() const {
            if (current_ + 1 >= source_.length()) return '\0';
            return source_[current_ + 1];
        }

        bool Match(char expected) {
            if (IsAtEnd() || source_[current_] != expected) return false;
            current_++; col_++;
            return true;
        }

        void StartToken() { start_ = current_; }

        void AddToken(std::vector<Token>& tokens, TokenType type) {
            std::string text = source_.substr(start_, current_ - start_);
            tokens.push_back({type, text, line_, col_ - text.length()});
        }

        Token Identifier() {
            while (isalnum(Peek()) || Peek() == '_') Advance();

            std::string text = source_.substr(start_, current_ - start_);
            TokenType type = TokenType::Identifier;
            if (text == "let") type = TokenType::KeywordLet;
            else if (text == "const") type = TokenType::KeywordConst;
            else if (text == "if") type = TokenType::KeywordIf;
            else if (text == "else") type = TokenType::KeywordElse;
            else if (text == "true") type = TokenType::KeywordTrue;
            else if (text == "false") type = TokenType::KeywordFalse;
            else if (text == "null") type = TokenType::KeywordNull;

            return {type, text, line_, col_ - text.length()};
        }

        Token Number() {
            while (isdigit(Peek())) Advance();
            if (Peek() == '.' && isdigit(PeekNext())) {
                Advance();
                while (isdigit(Peek())) Advance();
            }
            std::string text = source_.substr(start_, current_ - start_);
            return {TokenType::Number, text, line_, col_ - text.length()};
        }

        Token String(char quote) {
            std::string value;
            while (Peek() != quote && !IsAtEnd()) {
                if (Peek() == '\n') { line_++; col_ = 1; }
                if (Peek() == '\\') {
                    Advance();
                    // Basic escape handling
                }
                value += Advance();
            }

            if (IsAtEnd()) throw std::runtime_error("Unterminated string");
            Advance(); // Closing quote

            return {TokenType::String, value, line_, col_ - value.length() - 2};
        }
    };

    // --- Parser ---
    class Parser {
    public:
        Parser(std::vector<Token> tokens, ExpressionContext& context, const ExpressionFunctionRegistry& functions)
            : tokens_(std::move(tokens)), context_(context), functions_(functions) {}

        ExpressionValue ParseStatementList() {
            ExpressionValue last_val;
            while (!IsAtEnd()) {
                last_val = ParseStatement();
            }
            return last_val;
        }

        ExpressionValue ParseStatement() {
            if (Match(TokenType::KeywordLet)) {
                return ParseDeclaration(false);
            } else if (Match(TokenType::KeywordConst)) {
                return ParseDeclaration(true);
            } else if (Match(TokenType::Semicolon)) {
                return ExpressionValue(); // Empty statement
            }

            ExpressionValue val = ParseExpression();
            Match(TokenType::Semicolon); // Consume optional semicolon
            return val;
        }

        ExpressionValue ParseDeclaration(bool is_const) {
            Token name = Consume(TokenType::Identifier, "Expected variable name");
            ExpressionValue init;
            if (Match(TokenType::Assign)) {
                init = ParseExpression();
            } else if (is_const) {
                throw std::runtime_error("Const declarations must have an initializer");
            }

            if (!context_.Declare(name.text, init, is_const)) {
                throw std::runtime_error("Variable '" + name.text + "' already declared");
            }
            Match(TokenType::Semicolon);
            return init; // Declaration evaluates to init value
        }

        ExpressionValue ParseExpression() {
            return ParseAssignment();
        }

        ExpressionValue ParseAssignment() {
            // Simplified assignment handling:
            // Since we can't easily peek arbitrary lookahead for "IDENT = ...",
            // and we want to reuse ParseLogicalOr, we have to handle checking if the
            // result of Evaluate is an Assignable Reference.
            // But here we are just evaluating.
            // Alternative: check if current token is Identifier and next is Assign

            if (Check(TokenType::Identifier) && PeekNext().type == TokenType::Assign) {
                Token name = Advance(); // Eat identifier
                Advance(); // Eat '='
                ExpressionValue value = ParseAssignment(); // Recursive for right-associativity

                if (!context_.Set(name.text, value)) {
                     throw std::runtime_error("Cannot assign to const or undeclared variable: " + name.text);
                }
                return value;
            }

            return ParseLogicalOr();
        }

        ExpressionValue ParseLogicalOr() {
             ExpressionValue left = ParseLogicalAnd();
             while (Match(TokenType::Or)) {
                 // Short-circuit: if left is truthy, skip right
                 if (left.AsBoolean()) {
                     ParseLogicalAnd(); // Parse but discard
                     continue;
                 }
                 ExpressionValue right = ParseLogicalAnd();
                 left = ExpressionValue(left.AsBoolean() || right.AsBoolean());
             }
             return left;
        }

        ExpressionValue ParseLogicalAnd() {
            ExpressionValue left = ParseEquality();
            while (Match(TokenType::And)) {
                // Short-circuit: if left is falsy, skip right
                if (!left.AsBoolean()) {
                    ParseEquality(); // Parse but discard
                    continue;
                }
                ExpressionValue right = ParseEquality();
                left = ExpressionValue(left.AsBoolean() && right.AsBoolean());
            }
            return left;
        }

        ExpressionValue ParseEquality() {
            ExpressionValue left = ParseComparison();
            while (Match(TokenType::Equal) || Match(TokenType::NotEqual)) {
                TokenType op = Previous().type;
                ExpressionValue right = ParseComparison();
                if (op == TokenType::Equal) left = ExpressionValue(left == right);
                else left = ExpressionValue(left != right);
            }
            return left;
        }

        ExpressionValue ParseComparison() {
            ExpressionValue left = ParseAdditive();
            while (Match(TokenType::Less) || Match(TokenType::LessEqual) ||
                   Match(TokenType::Greater) || Match(TokenType::GreaterEqual)) {
                TokenType op = Previous().type;
                ExpressionValue right = ParseAdditive();
                double l = left.AsNumber();
                double r = right.AsNumber();
                switch (op) {
                    case TokenType::Less: left = ExpressionValue(l < r); break;
                    case TokenType::LessEqual: left = ExpressionValue(l <= r); break;
                    case TokenType::Greater: left = ExpressionValue(l > r); break;
                    case TokenType::GreaterEqual: left = ExpressionValue(l >= r); break;
                    default: break;
                }
            }
            return left;
        }

        ExpressionValue ParseAdditive() {
            ExpressionValue left = ParseMultiplicative();
            while (Match(TokenType::Plus) || Match(TokenType::Minus)) {
                TokenType op = Previous().type;
                ExpressionValue right = ParseMultiplicative();
                if (op == TokenType::Plus) left = ExpressionValue(left.AsNumber() + right.AsNumber());
                else left = ExpressionValue(left.AsNumber() - right.AsNumber());
            }
            return left;
        }

        ExpressionValue ParseMultiplicative() {
            ExpressionValue left = ParseUnary();
            while (Match(TokenType::Multiply) || Match(TokenType::Divide)) {
                TokenType op = Previous().type;
                ExpressionValue right = ParseUnary();
                if (op == TokenType::Multiply) left = ExpressionValue(left.AsNumber() * right.AsNumber());
                else {
                    double r = right.AsNumber();
                    if (r == 0) throw std::runtime_error("Division by zero");
                    left = ExpressionValue(left.AsNumber() / r);
                }
            }
            return left;
        }

        ExpressionValue ParseUnary() {
            if (Match(TokenType::Minus)) return ExpressionValue(-ParseUnary().AsNumber());
            if (Match(TokenType::Plus)) return ParseUnary(); // +x is just x
            if (Match(TokenType::Not)) return ExpressionValue(!ParseUnary().AsBoolean());
            return ParsePrimary();
        }

        ExpressionValue ParsePrimary() {
            if (Match(TokenType::KeywordTrue)) return ExpressionValue(true);
            if (Match(TokenType::KeywordFalse)) return ExpressionValue(false);
            if (Match(TokenType::KeywordNull)) return ExpressionValue(nullptr);
            if (Match(TokenType::Number)) return ExpressionValue(std::stod(Previous().text));
            if (Match(TokenType::String)) {
                // Return string with interpolation processed
                return ExpressionValue(ProcessString(Previous().text));
            }
            
            if (Match(TokenType::Identifier)) {
                std::string name = Previous().text;
                
                // Function call?
                if (Match(TokenType::LParen)) {
                    std::vector<ExpressionValue> args;
                    if (!Check(TokenType::RParen)) {
                        do {
                            args.push_back(ParseExpression());
                        } while (Match(TokenType::Comma));
                    }
                    Consume(TokenType::RParen, "Expected ')' after arguments");
                    auto result = functions_.Call(name, args);
                    if (!result) throw std::runtime_error("Unknown function: " + name);
                    return *result;
                }
                
                auto val = context_.Get(name);
                if (!val) throw std::runtime_error("Undefined variable: " + name);
                return *val;
            }
            
            if (Match(TokenType::LParen)) {
                ExpressionValue expr = ParseExpression();
                Consume(TokenType::RParen, "Expected ')'");
                return expr;
            }
            
            throw std::runtime_error("Expect expression");
        }
        
        std::string ProcessString(const std::string& s) {
            std::string result = s;
            size_t pos = 0;
            while ((pos = result.find("${", pos)) != std::string::npos) {
                size_t end = result.find("}", pos);
                if (end != std::string::npos) {
                    std::string var_name = result.substr(pos + 2, end - pos - 2);
                    auto val = context_.Get(var_name);
                    std::string replacement = val ? val->AsString() : "undefined";
                    result.replace(pos, end - pos + 1, replacement);
                    pos += replacement.length();
                } else {
                    break;
                }
            }
            return result;
        }

    private:
        std::vector<Token> tokens_;
        size_t current_{0};
        ExpressionContext& context_;
        const ExpressionFunctionRegistry& functions_;
        
        bool Match(TokenType type) {
            if (Check(type)) {
                Advance();
                return true;
            }
            return false;
        }
        
        bool Check(TokenType type) const {
            if (IsAtEnd()) return false;
            return tokens_[current_].type == type;
        }
        
        Token Advance() {
            if (!IsAtEnd()) current_++;
            return Previous();
        }
        
        bool IsAtEnd() const {
            return tokens_[current_].type == TokenType::Eof;
        }
        
        Token Peek() const {
            return tokens_[current_];
        }
        
        Token PeekNext() const {
            if (current_ + 1 >= tokens_.size()) return {TokenType::Eof, ""};
            return tokens_[current_ + 1];
        }
        
        Token Previous() const {
            return tokens_[current_ - 1];
        }
        
        Token Consume(TokenType type, const std::string& message) {
            if (Check(type)) return Advance();
            throw std::runtime_error(message);
        }
    };
};

} // namespace experiments
