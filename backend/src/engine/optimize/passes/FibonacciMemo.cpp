#include "FibonacciMemo.hpp"
#include "../SummaryBuilder.hpp"
#include "engine/ast/Node.hpp"

#include <string>
#include <vector>

namespace nc::optimize {

using nc::ast::Node;

namespace {

void collect(const Node& n, const std::string& type, std::vector<const Node*>& out) {
    if (n.type == type) out.push_back(&n);
    for (const Node& c : n.children) collect(c, type, out);
}

void collectCallsNamed(const Node& n, const std::string& name, std::vector<const Node*>& out) {
    if (n.type == "CALL_EXPR" && n.spelling == name) out.push_back(&n);
    for (const Node& c : n.children) collectCallsNamed(c, name, out);
}

} // namespace

// Matches: a function that calls itself at least twice in its body, combined
// with "+" (the classic fib(n-1) + fib(n-2) shape), with exactly one
// parameter. Only fires when the problem's declared operation is "fibonacci".
static bool matchesNaiveFib(const PassContext& ctx) {
    if (ctx.operation != "fibonacci") return false;

    const Node* root = static_cast<const Node*>(ctx.ast);
    if (!root) return false;

    std::vector<const Node*> fns;
    collect(*root, "FUNCTION_DECL", fns);

    for (const Node* fn : fns) {
        if (fn->spelling.empty()) continue;

        int params = 0;
        for (const Node& c : fn->children) params += (c.type == "PARM_DECL");
        if (params != 1) continue;

        std::vector<const Node*> selfCalls;
        collectCallsNamed(*fn, fn->spelling, selfCalls);
        if (selfCalls.size() < 2) continue;

        std::vector<const Node*> plusOps;
        collect(*fn, "BINARY_OPERATOR", plusOps);
        bool hasPlusOfCalls = false;
        for (const Node* p : plusOps) {
            if (!p->op || *p->op != "+") continue;
            hasPlusOfCalls = true;
        }
        if (!hasPlusOfCalls) continue;

        return true;
    }
    return false;
}

std::optional<OptimizationResult> FibonacciMemo::tryApply(const PassContext& ctx) const {
    if (!matchesNaiveFib(ctx)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace recursive Fibonacci with iterative memoization";
    r.rationale = "Naive recursion recomputes the same values exponentially many times; "
                  "building the sequence once, bottom-up, computes each value exactly once.";
    r.beforeComplexity = "O(2^n)";
    r.afterComplexity = "O(n)";
    r.rewrittenCode = sb.build("fibonacci_memo.cpp.tmpl", "", "");
    return r;
}

} // namespace nc::optimize
