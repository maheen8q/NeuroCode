// Line-for-line port of backend/tools/features.py (v2, 53 features). When this file and features.py
// disagree, features.py wins and this file is the bug. tests/parity_ast.py diffs the two on every
// sample; run it after any change to either file.
//
// Behaviours kept on purpose because the model was trained on them:
//   - loop features read the first 40 tokens of the loop, which include the start of nested loops
//   - vector/map indexing is CALL_EXPR "operator[]" and counts as array access
//   - container counts apply to VAR_DECL / PARM_DECL / FIELD_DECL only
//   - cyclomatic = ifs + loops + ternaries + switches + 1 (decision points)
//   - recursion = a FUNCTION_DECL whose name also appears as any CALL_EXPR spelling
//   - "variable name" means a DECL_REF_EXPR spelling, looking through single-child UNEXPOSED_EXPR
#include "ml/FeatureExtractor.hpp"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "engine/ast/Serializer.hpp"

namespace nc {

using ast::Node;

namespace {

enum Idx : int {
    kLoopCount, kForCount, kWhileCount, kRangeForCount,
    kMaxNestDepth, kLoopBoundSizeCall, kLoopBoundVar,
    kLoopFromIPlus1, kLoopStepHalving, kLoopStepDoubling,
    kHasRecursion, kRecursiveFnCount, kSelfCallCount, kFnCount,
    kIfCount, kElseCount, kTernaryCount, kSwitchCount,
    kBreakCount, kContinueCount, kCyclomatic,
    kVectorCount, kVector2dCount, kSetCount, kUnorderedSetCount,
    kMapCount, kUnorderedMapCount, kQueueCount, kStackCount,
    kPriorityQueueCount, kStringCount, kPairCount,
    kSortCalls, kLowerBoundCalls, kUpperBoundCalls, kFindCalls,
    kMaxElementCalls, kPushPopCalls, kCountFindMemberCalls,
    kArrayAccessCount, kIndexI, kIndexIPlus1, kIndexIMinus1,
    kIndexMid, kTwoIndexSameArray,
    kAddSubCount, kMulDivCount, kCompareCount,
    kLoopCondTwoVars, kIncAndDecInLoop, kRunningAddSub,
    kIndexMinusVar, kInnerWhileNoReset,
    kCount
};
static_assert(kCount == kFeatureVectorLength, "feature enum and vector length disagree");

bool isLoop(const std::string& t) {
    return t == "FOR_STMT" || t == "WHILE_STMT" || t == "DO_STMT" || t == "CXX_FOR_RANGE_STMT";
}

bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

std::string withoutSpaces(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());   // Python: s.replace(" ", "")
    return s;
}

template <class Fn>
void walk(const Node& n, Fn&& fn) {                            // pre-order, like features._walk
    fn(n);
    for (const auto& c : n.children) walk(c, fn);
}

// ---- helpers of the v2 pointer features -----------------------------------------------------

const Node& unwrap(const Node* n) {
    while (n->type == "UNEXPOSED_EXPR" && n->children.size() == 1) n = &n->children[0];
    return *n;
}

// "" means None. Every use in features.py is a truthiness test, so "" and None behave the same.
std::string varName(const Node& n) {
    const Node& u = unwrap(&n);
    return u.type == "DECL_REF_EXPR" ? u.spelling : std::string();
}

bool isSubscript(const Node& n) {
    return n.type == "ARRAY_SUBSCRIPT_EXPR" || (n.type == "CALL_EXPR" && n.spelling == "operator[]");
}

bool containsSubscript(const Node& n) {
    bool found = false;
    walk(n, [&](const Node& x) { if (isSubscript(x)) found = true; });
    return found;
}

bool isCompare(const std::string& op) {
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

std::set<std::string> modifiedVars(const Node& n) {
    std::set<std::string> mod;
    walk(n, [&](const Node& x) {
        const std::string op = x.op.value_or("");
        if (x.children.empty()) return;
        if (x.type == "UNARY_OPERATOR" && (op == "++" || op == "--")) {
            const std::string name = varName(x.children[0]);
            if (!name.empty()) mod.insert(name);
        } else if ((x.type == "COMPOUND_ASSIGNMENT_OPERATOR" || x.type == "BINARY_OPERATOR") &&
                   (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=")) {
            const std::string name = varName(x.children[0]);
            if (!name.empty()) mod.insert(name);
        }
    });
    return mod;
}

const Node* loopCondition(const Node& loop) {
    const auto& kids = loop.children;
    if (loop.type == "WHILE_STMT") return kids.empty() ? nullptr : &kids.front();
    if (loop.type == "DO_STMT") return kids.empty() ? nullptr : &kids.back();
    if (loop.type == "FOR_STMT") {
        for (const auto& k : kids) {                           // init, cond, inc, body (missing parts omitted)
            const Node& u = unwrap(&k);
            if (u.type == "BINARY_OPERATOR") {
                const std::string op = u.op.value_or("");
                if (isCompare(op) || op == "&&" || op == "||") return &k;
            }
        }
    }
    return nullptr;
}

struct Visit { const Node* node; const Node* parent; int loops_above; };

void collect(const Node& n, const Node* parent, int loops_above, std::vector<Visit>& out) {
    out.push_back({&n, parent, loops_above});
    const int next = loops_above + (isLoop(n.type) ? 1 : 0);
    for (const auto& c : n.children) collect(c, &n, next, out);
}

void pointerFeatures(const Node& root, std::vector<float>& v) {
    static const std::regex index_minus_var(R"([A-Za-z_]\w*\s*-\s*[A-Za-z_]\w*)");
    int two_vars = 0, inc_dec = 0, running = 0, no_reset = 0;
    std::set<std::string> adds, subs;

    std::vector<Visit> order;
    collect(root, nullptr, 0, order);

    for (const Visit& vis : order) {
        const Node& n = *vis.node;
        const std::string op = n.op.value_or("");
        const auto& kids = n.children;

        if (isLoop(n.type)) {
            const std::set<std::string> mod = modifiedVars(n);
            const Node* cond = loopCondition(n);

            // 48: condition compares two variables that both move inside the loop (l < r)
            if (cond) {
                bool hit = false;
                walk(*cond, [&](const Node& x) {
                    if (hit) return;                           // Python breaks at the first hit
                    if (x.type == "BINARY_OPERATOR" && isCompare(x.op.value_or("")) && x.children.size() == 2) {
                        const std::string a = varName(x.children[0]), b = varName(x.children[1]);
                        if (!a.empty() && !b.empty() && a != b && mod.count(a) && mod.count(b)) {
                            ++two_vars;
                            hit = true;
                        }
                    }
                });
            }

            // 49: one index incremented and a different one decremented in the same loop
            std::set<std::string> ups, downs;
            walk(n, [&](const Node& x) {
                if (x.type == "UNARY_OPERATOR" && !x.children.empty()) {
                    const std::string name = varName(x.children[0]);
                    if (!name.empty() && x.op.value_or("") == "++") ups.insert(name);
                    if (!name.empty() && x.op.value_or("") == "--") downs.insert(name);
                }
            });
            if (!ups.empty() && !downs.empty()) {
                const bool up_only = std::any_of(ups.begin(), ups.end(), [&](const std::string& s) { return !downs.count(s); });
                const bool down_only = std::any_of(downs.begin(), downs.end(), [&](const std::string& s) { return !ups.count(s); });
                if (up_only && down_only) ++inc_dec;
            }

            // 52: a while nested in a loop whose condition variable is not re-declared just before it
            if (n.type == "WHILE_STMT" && vis.loops_above > 0 && cond) {
                std::set<std::string> cvars;
                walk(*cond, [&](const Node& x) { if (x.type == "DECL_REF_EXPR") cvars.insert(x.spelling); });
                bool reset = false;
                if (vis.parent) {
                    for (const auto& sib : vis.parent->children) {
                        if (&sib == &n) break;
                        walk(sib, [&](const Node& x) {
                            if (x.type == "VAR_DECL" && cvars.count(x.spelling)) reset = true;
                        });
                    }
                }
                if (!reset) ++no_reset;
            }
        }

        // 50: running window total: x += a[..] and x -= a[..] on the same x, or x += a[..] - a[..]
        if (n.type == "COMPOUND_ASSIGNMENT_OPERATOR" && kids.size() == 2) {
            const std::string lhs = varName(kids[0]);
            const Node& rhs = kids[1];
            if (!lhs.empty() && containsSubscript(rhs)) {
                if (op == "+=") adds.insert(lhs);
                if (op == "-=") subs.insert(lhs);
                const Node& u = unwrap(&rhs);
                if (op == "+=" && u.type == "BINARY_OPERATOR" && u.op.value_or("") == "-" &&
                    u.children.size() == 2 && containsSubscript(u.children[0]) && containsSubscript(u.children[1]))
                    ++running;
            }
        }

        // 51: index minus a variable, a[i - k] (but not a[i - 1])
        if (isSubscript(n)) {
            if (std::regex_search(n.index_text.value_or(""), index_minus_var)) v[kIndexMinusVar] += 1.f;
        }
    }

    for (const auto& s : adds) if (subs.count(s)) ++running;   // len(adds & subs)

    v[kLoopCondTwoVars] = static_cast<float>(two_vars);
    v[kIncAndDecInLoop] = static_cast<float>(inc_dec);
    v[kRunningAddSub] = static_cast<float>(running);
    v[kInnerWhileNoReset] = static_cast<float>(no_reset);
}

} // namespace

const std::array<const char*, kFeatureVectorLength>& featureNames() {
    static const std::array<const char*, kFeatureVectorLength> names = {
        "loop_count", "for_count", "while_count", "rangefor_count",
        "max_nest_depth", "loop_bound_size_call", "loop_bound_var",
        "loop_from_i_plus_1", "loop_step_halving", "loop_step_doubling",
        "has_recursion", "recursive_fn_count", "self_call_count", "fn_count",
        "if_count", "else_count", "ternary_count", "switch_count",
        "break_count", "continue_count", "cyclomatic",
        "vector_count", "vector2d_count", "set_count", "unordered_set_count",
        "map_count", "unordered_map_count", "queue_count", "stack_count",
        "priority_queue_count", "string_count", "pair_count",
        "sort_calls", "lower_bound_calls", "upper_bound_calls", "find_calls",
        "max_element_calls", "push_pop_calls", "count_find_member_calls",
        "array_access_count", "index_i", "index_i_plus_1", "index_i_minus_1",
        "index_mid", "two_index_same_array",
        "add_sub_count", "mul_div_count", "compare_count",
        "loop_cond_two_vars", "inc_and_dec_in_loop", "running_add_sub",
        "index_minus_var", "inner_while_no_reset"};
    return names;
}

std::vector<float> computeFeatures(const Node& root) {
    std::vector<float> v(kFeatureVectorLength, 0.f);

    int max_loop_depth = 0;
    std::map<std::string, std::set<std::string>> indices_by_array;
    std::set<std::string> fns;
    std::vector<std::string> calls;

    // pre-order with the number of loop ancestors, so "1 + ancestors" is the nesting depth
    struct Rec {
        std::vector<float>& v; int& max_loop_depth;
        std::map<std::string, std::set<std::string>>& indices_by_array;
        std::set<std::string>& fns; std::vector<std::string>& calls;

        void operator()(const Node& node, int loops_above) const {
            const std::string& k = node.type;
            const std::string& sp = node.spelling;
            const std::string tp = node.dtype.value_or("");
            const std::string tok = node.tokens.value_or("");
            auto bump = [&](Idx i) { v[i] += 1.f; };

            if (isLoop(k)) {
                bump(kLoopCount);
                if (k == "FOR_STMT") bump(kForCount);
                else if (k == "WHILE_STMT" || k == "DO_STMT") bump(kWhileCount);
                else bump(kRangeForCount);
                max_loop_depth = std::max(max_loop_depth, 1 + loops_above);
                const std::string flat = withoutSpaces(tok);
                if (has(flat, ".size()") || has(flat, "size_t")) bump(kLoopBoundSizeCall);
                if (has(flat, "+1")) bump(kLoopFromIPlus1);
                if (has(flat, "/2") || has(flat, ">>1")) bump(kLoopStepHalving);
                if (has(flat, "*2") || has(flat, "<<1")) bump(kLoopStepDoubling);
            } else if (k == "IF_STMT") {
                bump(kIfCount);
                if (node.has_else.value_or(false)) bump(kElseCount);
            } else if (k == "CONDITIONAL_OPERATOR") {
                bump(kTernaryCount);
            } else if (k == "SWITCH_STMT") {
                bump(kSwitchCount);
            } else if (k == "BREAK_STMT") {
                bump(kBreakCount);
            } else if (k == "CONTINUE_STMT") {
                bump(kContinueCount);
            } else if (k == "FUNCTION_DECL") {
                bump(kFnCount);
            } else if (k == "ARRAY_SUBSCRIPT_EXPR" || (k == "CALL_EXPR" && sp == "operator[]")) {
                bump(kArrayAccessCount);
                const std::string idx = node.index_text.value_or("");
                const std::string base = node.base_text.value_or("");
                if (idx == "i" || idx == "idx" || idx == "p") bump(kIndexI);
                if (has(idx, "+ 1") || has(idx, "+1")) bump(kIndexIPlus1);
                if (has(idx, "- 1") || has(idx, "-1")) bump(kIndexIMinus1);
                if (has(idx, "mid") || has(idx, "middle")) bump(kIndexMid);
                indices_by_array[base].insert(idx);
            } else if (k == "CALL_EXPR") {
                if (sp == "sort") bump(kSortCalls);
                else if (sp == "lower_bound") bump(kLowerBoundCalls);
                else if (sp == "upper_bound") bump(kUpperBoundCalls);
                else if (sp == "find") bump(kFindCalls);
                else if (sp == "max_element" || sp == "min_element") bump(kMaxElementCalls);
                else if (sp == "push" || sp == "pop" || sp == "push_back" || sp == "pop_back" ||
                         sp == "push_front" || sp == "emplace") bump(kPushPopCalls);
                else if (sp == "count" || sp == "contains") bump(kCountFindMemberCalls);
            } else if (k == "BINARY_OPERATOR") {
                const std::string op = node.op.value_or("");
                if (op == "+" || op == "-" || op == "+=" || op == "-=") bump(kAddSubCount);
                else if (op == "*" || op == "/" || op == "%" || op == "*=" || op == "/=") bump(kMulDivCount);
                else if (isCompare(op)) bump(kCompareCount);
            }

            // container types, counted on declarations only
            if (!tp.empty() && (k == "VAR_DECL" || k == "PARM_DECL" || k == "FIELD_DECL")) {
                const std::string t = withoutSpaces(tp);
                if (has(t, "vector<vector<")) bump(kVector2dCount);
                else if (has(t, "vector<")) bump(kVectorCount);
                if (has(t, "unordered_set<")) bump(kUnorderedSetCount);
                else if (has(t, "set<")) bump(kSetCount);
                if (has(t, "unordered_map<")) bump(kUnorderedMapCount);
                else if (has(t, "map<")) bump(kMapCount);
                if (has(t, "priority_queue<")) bump(kPriorityQueueCount);
                else if (has(t, "queue<")) bump(kQueueCount);
                if (has(t, "stack<")) bump(kStackCount);
                if (has(t, "string")) bump(kStringCount);
                if (has(t, "pair<")) bump(kPairCount);
            }

            if (k == "FUNCTION_DECL") fns.insert(sp);
            if (k == "CALL_EXPR") calls.push_back(sp);

            const int next = loops_above + (isLoop(k) ? 1 : 0);
            for (const auto& c : node.children) (*this)(c, next);
        }
    };
    Rec{v, max_loop_depth, indices_by_array, fns, calls}(root, 0);

    v[kMaxNestDepth] = static_cast<float>(max_loop_depth);
    v[kLoopBoundVar] = v[kLoopCount] - v[kLoopBoundSizeCall];

    // recursion: a function whose name is also called somewhere
    std::set<std::string> rec;
    for (const auto& c : calls) if (fns.count(c)) rec.insert(c);
    v[kHasRecursion] = rec.empty() ? 0.f : 1.f;
    v[kRecursiveFnCount] = static_cast<float>(rec.size());
    v[kSelfCallCount] = static_cast<float>(std::count_if(calls.begin(), calls.end(),
                                                         [&](const std::string& c) { return rec.count(c) > 0; }));

    // two different indices into the same array
    v[kTwoIndexSameArray] = static_cast<float>(std::count_if(
        indices_by_array.begin(), indices_by_array.end(),
        [](const auto& kv) { return kv.second.size() >= 2; }));

    v[kCyclomatic] = v[kIfCount] + v[kLoopCount] + v[kTernaryCount] + v[kSwitchCount] + 1.f;

    pointerFeatures(root, v);
    return v;
}

std::vector<float> computeFeaturesFromJson(const std::string& ast_json) {
    try {
        return computeFeatures(ast::fromJson(nlohmann::json::parse(ast_json)));
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("invalid AST JSON: ") + e.what());
    }
}

} // namespace nc
