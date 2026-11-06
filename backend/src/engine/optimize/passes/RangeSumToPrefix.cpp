#include "RangeSumToPrefix.hpp"
#include "../SummaryBuilder.hpp"
#include "engine/ast/Node.hpp"

#include <string>
#include <vector>

namespace nc::optimize {

using nc::ast::Node;

namespace {

const Node* firstOfType(const Node& n, const std::string& type) {
    if (n.type == type) return &n;
    for (const Node& c : n.children)
        if (const Node* f = firstOfType(c, type)) return f;
    return nullptr;
}

void collect(const Node& n, const std::string& type, std::vector<const Node*>& out) {
    if (n.type == type) out.push_back(&n);
    for (const Node& c : n.children) collect(c, type, out);
}
void collectLoops(const Node& n, std::vector<const Node*>& out) {
    if (n.type == "FOR_STMT" || n.type == "CXX_FOR_RANGE_STMT") out.push_back(&n);
    for (const Node& c : n.children) collectLoops(c, out);
}

const Node* firstLoop(const Node& n) {
    if (n.type == "FOR_STMT" || n.type == "CXX_FOR_RANGE_STMT") return &n;
    for (const Node& c : n.children)
        if (const Node* f = firstLoop(c)) return f;
    return nullptr;
}
void collectSubscripts(const Node& n, std::vector<const Node*>& out) {
    if (n.type == "CALL_EXPR" && n.spelling == "operator[]" && n.base_text.has_value())
        out.push_back(&n);
    for (const Node& c : n.children) collectSubscripts(c, out);
}

} // namespace

// Matches: an outer loop (over queries) containing a nested inner loop that
// accumulates with += from a subscript on the array (a naive O(n) sum per
// query). Only fires when the problem's declared operation is "range_sum".
static bool matchesRangeSum(const PassContext& ctx, std::string& arr, std::string& /*target*/) {
    if (ctx.operation != "range_sum") return false;

    const Node* root = static_cast<const Node*>(ctx.ast);
    if (!root) return false;

    std::vector<const Node*> outerFors;
    collectLoops(*root, outerFors);

    for (const Node* outer : outerFors) {
        const Node* inner = nullptr;
        for (const Node& c : outer->children)
            if ((inner = firstLoop(c))) break;
        if (!inner) continue;

        // look for a += whose right side touches a subscript on some array
        std::vector<const Node*> compounds;
        collect(*inner, "COMPOUND_ASSIGNMENT_OPERATOR", compounds);
        for (const Node* ca : compounds) {
            if (!ca->op || *ca->op != "+=" ) continue;

            std::vector<const Node*> subs;
            collectSubscripts(*ca, subs);
            if (subs.empty()) continue;

            arr = *subs.front()->base_text;
            return true;
        }
    }
    return false;
}

std::optional<OptimizationResult> RangeSumToPrefix::tryApply(const PassContext& ctx) const {
    std::string arr, unused;
    if (!matchesRangeSum(ctx, arr, unused)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace per-query summation with a prefix-sum array";
    r.rationale = "A prefix-sum array lets each query answer in O(1), so the total "
                  "work drops from O(n) per query to O(n) once, plus O(1) per query.";
    r.beforeComplexity = "O(n*q)";
    r.afterComplexity = "O(n+q)";
    r.rewrittenCode = sb.build("range_sum_prefix.cpp.tmpl", arr, "");
    return r;
}

} // namespace nc::optimize
