#pragma once
#include <optional>
#include <string>

namespace nc::optimize {

// A single detected-and-rewritten improvement.
// NOTE: field names must match T4's engine/report struct. If OptimizationResult
// is already defined elsewhere, delete this struct and include that header.
struct OptimizationResult {
    std::string passName;         // "PairSumToHash"
    std::string title;            // user-facing: "Replace nested loop with hash lookup"
    std::string rationale;        // why it is faster
    std::string beforeComplexity; // "O(n^2)"
    std::string afterComplexity;  // "O(n)"
    std::string rewrittenCode;    // suggestion, filled from a template
    double confidence = 1.0;      // rule-based passes are certain
};

// Minimal read-only view a pass needs. Replace the opaque pointers with T4's
// real AST/CFG handles once available; kept void* so this header stays free of libclang.
struct PassContext {
    const void* ast = nullptr;    // T4: clang-shaped AST root
    const void* cfg = nullptr;    // T4: control flow graph
    std::string sourceCode;       // original user code, already I/O-stripped
};

class IPass {
public:
    virtual ~IPass() = default;
    virtual std::string name() const = 0;
    // Returns a result only when the pattern is found, else nullopt.
    virtual std::optional<OptimizationResult> tryApply(const PassContext& ctx) const = 0;
};

} // namespace nc::optimize
