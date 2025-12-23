#pragma once

/**
 * error_formatter.hpp - Pretty-print errors with source context
 */

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace experiments {

struct StackFrame {
    std::string function_name;
    std::string file_path;
    int line{0};
    int column{0};
    bool is_native{false};
    bool is_constructor{false};
    bool is_async{false};
    
    std::string ToString() const {
        std::ostringstream oss;
        oss << "    at ";
        if (is_async) oss << "async ";
        if (is_constructor) oss << "new ";
        if (!function_name.empty()) {
            oss << function_name << " ";
        }
        
        if (is_native) {
            oss << "(native)";
        } else {
            oss << "(" << file_path;
            if (line > 0) {
                oss << ":" << line;
                if (column > 0) {
                    oss << ":" << column;
                }
            }
            oss << ")";
        }
        return oss.str();
    }
};

class ErrorFormatter {
public:
    struct Options {
        bool colorize{true};
        bool show_source{true};
        int context_lines{3};
        bool show_column_indicator{true};
        bool apply_sourcemap{true};
        size_t max_stack_frames{20};
        std::string ansi_red{"\033[31m"};
        std::string ansi_yellow{"\033[33m"};
        std::string ansi_cyan{"\033[36m"};
        std::string ansi_gray{"\033[90m"};
        std::string ansi_reset{"\033[0m"};
    };
    
    explicit ErrorFormatter(Options options = {}) : options_(std::move(options)) {}
    
    std::string Format(const std::string& error_type,
                       const std::string& message,
                       const std::vector<StackFrame>& stack) const {
        std::ostringstream oss;
        
        if (options_.colorize) {
            oss << options_.ansi_red << error_type << ": " << options_.ansi_reset;
        } else {
            oss << error_type << ": ";
        }
        oss << message << "\n";
        
        size_t count = std::min(stack.size(), options_.max_stack_frames);
        for (size_t i = 0; i < count; ++i) {
            const auto& frame = stack[i];
            
            if (options_.colorize) {
                oss << options_.ansi_gray << frame.ToString() << options_.ansi_reset;
            } else {
                oss << frame.ToString();
            }
            oss << "\n";
            
            if (i == 0 && options_.show_source && !frame.is_native && !frame.file_path.empty()) {
                std::string snippet = GetSourceSnippet(frame.file_path, frame.line, frame.column);
                if (!snippet.empty()) {
                    oss << snippet;
                }
            }
        }
        
        if (stack.size() > options_.max_stack_frames) {
            oss << "    ... " << (stack.size() - options_.max_stack_frames) << " more frames\n";
        }
        
        return oss.str();
    }
    
    std::string GetSourceSnippet(const std::string& file_path, int line, int column) const {
        std::ifstream file(file_path);
        if (!file.is_open()) return "";
        
        std::vector<std::string> lines;
        std::string line_content;
        while (std::getline(file, line_content)) {
            lines.push_back(line_content);
        }
        
        if (line <= 0 || line > static_cast<int>(lines.size())) return "";
        
        std::ostringstream oss;
        int start = std::max(1, line - options_.context_lines);
        int end = std::min(static_cast<int>(lines.size()), line + options_.context_lines);
        
        for (int i = start; i <= end; ++i) {
            bool is_error_line = (i == line);
            std::string prefix;
            
            if (is_error_line) {
                prefix = options_.colorize ? (options_.ansi_red + "> " + options_.ansi_reset) : "> ";
            } else {
                prefix = "  ";
            }
            
            if (options_.colorize) {
                oss << options_.ansi_gray;
            }
            oss << std::setw(4) << i << " | ";
            if (options_.colorize) {
                oss << options_.ansi_reset;
            }
            
            oss << prefix << lines[i - 1] << "\n";
            
            if (is_error_line && options_.show_column_indicator && column > 0) {
                oss << "     | " << prefix;
                for (int c = 0; c < column - 1; ++c) oss << " ";
                if (options_.colorize) {
                    oss << options_.ansi_red << "^" << options_.ansi_reset;
                } else {
                    oss << "^";
                }
                oss << "\n";
            }
        }
        
        return oss.str();
    }
    
    std::string FormatSimple(const std::string& error_type, const std::string& message) const {
        if (options_.colorize) {
            return options_.ansi_red + error_type + ": " + options_.ansi_reset + message;
        }
        return error_type + ": " + message;
    }
    
    Options& GetOptions() { return options_; }
    
private:
    Options options_;
};

} // namespace experiments
