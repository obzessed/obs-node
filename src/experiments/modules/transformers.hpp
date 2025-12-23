#pragma once

/**
 * transformers.hpp - Module Transformation Classes
 */

#include <string>
#include <vector>
#include <memory>

#include "module_info.hpp"
#include "../core/logger.hpp"

namespace experiments {


//=============================================================================
// Transform Result - Output from a module transformer
//=============================================================================

struct TransformResult {
    std::string code;                       // Transformed source code
    std::optional<SourceMap> source_map;    // Optional source map
    std::vector<std::string> errors;        // Transform errors
    std::vector<std::string> warnings;      // Transform warnings

    bool is_ok() const { return errors.empty(); }

    // Apply source map inline
    std::string GetCodeWithInlineSourceMap() const {
        if (source_map && source_map->is_valid()) {
            return code + "\n" + source_map->ToInlineUrl();
        }
        return code;
    }
};

//=============================================================================
// Module Transformer - Abstract base for code transformations
//=============================================================================

class ModuleTransformer {
public:
    virtual ~ModuleTransformer() = default;

    // Transform source code
    virtual TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat format
    ) = 0;

    // Check if transformer should handle this file
    virtual bool ShouldTransform(const std::string& filename) const = 0;

    // Get transformer name
    virtual std::string GetName() const = 0;
};

using ModuleTransformerPtr = std::shared_ptr<ModuleTransformer>;

//=============================================================================
// TypeScript Transformer - Strips types and transforms TS to JS
//=============================================================================

class TypeScriptTransformer : public ModuleTransformer {
public:
    struct Options {
        bool jsx{false};                // Enable JSX support
        bool emit_decorator_metadata{false};
        bool use_define_for_class_fields{true};
        std::string jsx_factory{"React.createElement"};
        std::string jsx_fragment{"React.Fragment"};
        bool generate_source_map{true};
    };

    explicit TypeScriptTransformer(Options options = {})
        : options_(std::move(options)) {}

    TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat /*format*/
    ) override {
        TransformResult result;

        // Placeholder: In production, use SWC, esbuild, or TypeScript compiler API
        // This is a simplified type-stripping implementation for demonstration

        LOG_INFO("TypeScriptTransformer", "Transforming: " + filename);

        // For now, just pass through (in reality, strip types)
        result.code = StripTypes(source);

        if (options_.generate_source_map) {
            result.source_map = GenerateSourceMap(source, result.code, filename);
        }

        return result;
    }

    bool ShouldTransform(const std::string& filename) const override {
        return filename.ends_with(".ts") ||
               filename.ends_with(".tsx") ||
               filename.ends_with(".mts") ||
               filename.ends_with(".cts");
    }

    std::string GetName() const override { return "TypeScriptTransformer"; }

    Options& GetOptions() { return options_; }

private:
    Options options_;

    // Simple type stripping (placeholder - use real parser in production)
    std::string StripTypes(const std::string& source) {
        // This is a very simplified placeholder
        // Real implementation would use a proper TypeScript parser
        std::string result = source;

        // Remove type annotations like ": string", ": number", etc.
        // Remove interface definitions, type aliases, etc.
        // This is just for demonstration - not suitable for production

        return result;
    }

    SourceMap GenerateSourceMap(
        const std::string& /*original*/,
        const std::string& /*transformed*/,
        const std::string& filename
    ) {
        SourceMap map;
        map.version = 3;
        map.file = filename;
        map.sources.push_back(filename);
        map.mappings = "AAAA";  // Placeholder - would need proper VLQ encoding
        return map;
    }
};

//=============================================================================
// Transformer Chain - Applies multiple transformers in sequence
//=============================================================================

class TransformerChain : public ModuleTransformer {
public:
    void AddTransformer(ModuleTransformerPtr transformer) {
        transformers_.push_back(std::move(transformer));
    }

    TransformResult Transform(
        const std::string& source,
        const std::string& filename,
        ModuleFormat format
    ) override {
        TransformResult result;
        result.code = source;

        for (const auto& transformer : transformers_) {
            if (transformer->ShouldTransform(filename)) {
                auto step_result = transformer->Transform(result.code, filename, format);

                // Merge errors and warnings
                result.errors.insert(result.errors.end(),
                    step_result.errors.begin(), step_result.errors.end());
                result.warnings.insert(result.warnings.end(),
                    step_result.warnings.begin(), step_result.warnings.end());

                if (!step_result.is_ok()) break;

                result.code = step_result.code;
                if (step_result.source_map) {
                    // In production, merge source maps properly
                    result.source_map = step_result.source_map;
                }
            }
        }
        
        return result;
    }
    
    bool ShouldTransform(const std::string& filename) const override {
        for (const auto& transformer : transformers_) {
            if (transformer->ShouldTransform(filename)) return true;
        }
        return false;
    }
    
    std::string GetName() const override { return "TransformerChain"; }
    
private:
    std::vector<ModuleTransformerPtr> transformers_;
};

} // namespace experiments
