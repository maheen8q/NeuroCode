#include "PairSumToHash.hpp"
#include "../SummaryBuilder.hpp"
#include "engine/ast/Node.hpp"

#include <string>
#include <vector>

namespace nc::optimize {

using nc::ast::Node;

namespace {

// First descendant (or self) of the given type, or nullptr.
const Node* firstOfType(const Node& n, const std::string& type) {
    if (n.type == type) return &n;
    for (const Node& c : n.children)
        if (const Node* f = firstOfType(c, type)) return f;
    return nullptr;
}

// Collect every descendant (and self) of the given type.
void collect(const Node& n, const std::string& type, std::vector<const Node*>& out) {
    if (n.type == type) out.push_back(&n);
    for (const Node& c : n.children) collect(c, type, out);
}

// First "+" BINARY_OPERATOR anywhere in the subtree, or nullptr.
const Node* firstPlus(const Node& n) {
    if (n.type == "BINARY_OPERATOR" && n.op && *n.op == "+") return &n;
    for (const Node& c : n.children)
        if (const Node* p = firstPlus(c)) return p;
    return nullptr;
}

// Collect operator[] subscripts (they carry base_text / index_text).
void collectSubscripts(const Node& n, std::vector<const Node*>& out) {
    if (n.type == "CALL_EXPR" && n.spelling == "operator[]" && n.base_text.has_value())
        out.push_back(&n);
    for (const Node& c : n.children) collectSubscripts(c, out);
}

} // namespace

// Matches:  for (i ...) for (j = i+1 ...) if (a[i] + a[j] == target) ...
// On success: arr = container name, target = the thing compared against.
static bool matchesPairSum(const PassContext& ctx, std::string& arr, std::string& target) {
    const Node* root = static_cast<const Node*>(ctx.ast);
    if (!root) return false;

    std::vector<const Node*> fors;
    collect(*root, "FOR_STMT", fors);

    for (const Node* outer : fors) {
        // a FOR nested inside this one
        const Node* inner = nullptr;
        for (const Node& c : outer->children)
            if ((inner = firstOfType(c, "FOR_STMT"))) break;
        if (!inner) continue;

        // every "==" inside the inner loop
        std::vector<const Node*> eqs;
        collect(*inner, "BINARY_OPERATOR", eqs);
        for (const Node* eq : eqs) {
            if (!eq->op || *eq->op != "==" || eq->children.size() < 2) continue;

            // split the two sides: one holds a "+", the other is the target
            const Node* plusSide = nullptr;
            const Node* otherSide = nullptr;
            for (const Node& side : eq->children) {
                if (firstPlus(side)) plusSide = &side;
                else otherSide = &side;
            }
            if (!plusSide || !otherSide) continue;

            // the "+" must add two subscripts on the SAME container
            std::vector<const Node*> subs;
            collectSubscripts(*plusSide, subs);
            if (subs.size() < 2) continue;
            const std::string base = *subs[0]->base_text;
            bool sameBase = true;
            for (const Node* s : subs)
                if (*s->base_text != base) sameBase = false;
            if (!sameBase) continue;

            // target = first name on the other side (e.g. "k")
            const Node* ref = firstOfType(*otherSide, "DECL_REF_EXPR");
            arr = base;
            target = ref ? ref->spelling : "k";
            return true;
        }
    }
    return false;
}

std::optional<OptimizationResult> PairSumToHash::tryApply(const PassContext& ctx) const {
    std::string arr, target;
    if (!matchesPairSum(ctx, arr, target)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace nested loop with hash lookup";
    r.rationale = "Each element is checked against a hash set in O(1), "
                  "so the pair search drops from quadratic to linear time.";
    r.beforeComplexity = "O(n^2)";
    r.afterComplexity = "O(n)";
    r.rewrittenCode = sb.build("pair_sum_hash.cpp.tmpl", arr, target);
    return r;
}

} // namespace nc::optimize