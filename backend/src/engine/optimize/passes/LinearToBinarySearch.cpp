#include "LinearToBinarySearch.hpp"
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

} // namespace

// Matches: a single loop with no nested loop inside it, containing an "=="
// comparing a subscript (a[i]) against a target. Only fires when the problem's
// declared operation is "search_sorted": the AST alone cannot tell us the data
// is sorted, only the problem spec can promise that.
static bool matchesLinearScan(const PassContext& ctx, std::string& arr, std::string& target) {
    if (ctx.operation != "search_sorted") return false;

    const Node* root = static_cast<const Node*>(ctx.ast);
    if (!root) return false;

    std::vector<const Node*> fors;
    collect(*root, "FOR_STMT", fors);

    for (const Node* loop : fors) {
        bool hasNestedLoop = false;
        for (const Node& c : loop->children)
            if (firstOfType(c, "FOR_STMT")) hasNestedLoop = true;
        if (hasNestedLoop) continue;   // a nested loop means this isn't a plain scan

        std::vector<const Node*> eqs;
        collect(*loop, "BINARY_OPERATOR", eqs);
        for (const Node* eq : eqs) {
            if (!eq->op || *eq->op != "==" || eq->children.size() < 2) continue;

            const Node* subSide = nullptr;
            const Node* otherSide = nullptr;
            for (const Node& side : eq->children) {
                const Node* sub = firstOfType(side, "CALL_EXPR");
                if (sub && sub->spelling == "operator[]" && sub->base_text) {
                    subSide = sub;
                } else {
                    otherSide = &side;
                }
            }
            if (!subSide || !otherSide) continue;

            const Node* ref = firstOfType(*otherSide, "DECL_REF_EXPR");
            arr = *subSide->base_text;
            target = ref ? ref->spelling : "target";
            return true;
        }
    }
    return false;
}

std::optional<OptimizationResult> LinearToBinarySearch::tryApply(const PassContext& ctx) const {
    std::string arr, target;
    if (!matchesLinearScan(ctx, arr, target)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace linear scan with binary search";
    r.rationale = "The data is already sorted, so a binary search finds the "
                  "value in logarithmic time instead of scanning every element.";
    r.beforeComplexity = "O(n)";
    r.afterComplexity = "O(log n)";
    r.rewrittenCode = sb.build("linear_to_binary.cpp.tmpl", arr, target);
    return r;
}

} // namespace nc::optimize
