#include "engine/complexity/LoopAnalyzer.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "engine/ast/Traversal.hpp"

namespace nc::complexity {

using ast::Node;

namespace {

// ---------------------------------------------------------------------------------------------------------
// Small AST helpers
// ---------------------------------------------------------------------------------------------------------

const Node& unwrap(const Node& n) {
    const Node* p = &n;
    while ((p->type == "UNEXPOSED_EXPR" || p->type == "PAREN_EXPR" || p->type == "CSTYLE_CAST_EXPR" ||
            p->type == "CXX_STATIC_CAST_EXPR" || p->type == "CXX_FUNCTIONAL_CAST_EXPR") && !p->children.empty())
        p = &p->children.back();                            // a cast's last child is the casted expression
    return *p;
}

const std::string& opOf(const Node& n) {
    static const std::string empty;
    return n.op ? *n.op : empty;
}

bool isRef(const Node& n) { return unwrap(n).type == "DECL_REF_EXPR"; }
const std::string& refName(const Node& n) { return unwrap(n).spelling; }

void refsOf(const Node& n, std::set<std::string>& out) {
    if (n.type == "DECL_REF_EXPR") out.insert(n.spelling);
    for (const Node& c : n.children) refsOf(c, out);
}

std::set<std::string> refsOf(const Node& n) {
    std::set<std::string> s;
    refsOf(n, s);
    return s;
}

bool containsType(const Node& n, const std::string& type) {
    if (n.type == type) return true;
    for (const Node& c : n.children) if (containsType(c, type)) return true;
    return false;
}

bool containsCallNamed(const Node& n, const std::string& name) {
    if (n.type == "CALL_EXPR" && n.spelling == name) return true;
    for (const Node& c : n.children) if (containsCallNamed(c, name)) return true;
    return false;
}

bool isLiteralNode(const Node& n) {
    return n.type == "INTEGER_LITERAL" || n.type == "FLOATING_LITERAL" || n.type == "CXX_BOOL_LITERAL_EXPR" ||
           n.type == "CHARACTER_LITERAL" || n.type == "STRING_LITERAL" || n.type == "CXX_NULL_PTR_LITERAL_EXPR" ||
           n.type == "UNARY_EXPR" || n.type == "CXX_UNARY_EXPR" /* sizeof, alignof */;
}

// A number that does not depend on the input: literals, arithmetic on literals, names of known constants.
bool isConstantExpr(const Node& raw, const std::set<std::string>& constants) {
    const Node& n = unwrap(raw);
    if (isLiteralNode(n)) return true;
    if (n.type == "DECL_REF_EXPR") return constants.count(n.spelling) > 0;
    if (n.type == "BINARY_OPERATOR" || n.type == "UNARY_OPERATOR" || n.type == "CONDITIONAL_OPERATOR") {
        if (n.children.empty()) return false;
        for (const Node& c : n.children) if (!isConstantExpr(c, constants)) return false;
        return true;
    }
    return false;
}

bool isCompare(const std::string& op) { return op == "<" || op == "<=" || op == ">" || op == ">=" || op == "!=" || op == "=="; }

// ---------------------------------------------------------------------------------------------------------
// The header of a loop. libclang drops missing parts of a `for` header, so the children are
// [init?, cond?, inc?, body]; the loop's `tokens` text says which parts exist (same rules as the CFG builder).
// ---------------------------------------------------------------------------------------------------------

std::vector<std::string> headerSegments(const std::string& tokens) {
    std::istringstream in(tokens);
    std::string tok;
    int depth = 0;
    bool started = false;
    std::vector<std::string> seg(1);
    while (in >> tok) {
        if (tok == "(") {
            ++depth;
            if (!started) { started = true; continue; }
        } else if (tok == ")") {
            --depth;
            if (started && depth == 0) return seg;
        } else if (started && depth == 1 && tok == ";") {
            seg.emplace_back();
            continue;
        }
        if (started) seg.back() += tok;
    }
    return {};
}

struct ForParts {
    const Node* init = nullptr;
    const Node* cond = nullptr;
    const Node* inc = nullptr;
    const Node* body = nullptr;
    bool cond_is_true = false;
};

ForParts splitFor(const Node& n) {
    ForParts p;
    if (n.children.empty()) return p;
    p.body = &n.children.back();
    const size_t parts = n.children.size() - 1;
    const auto seg = headerSegments(n.tokens.value_or(""));
    const Node** target[3] = {&p.init, &p.cond, &p.inc};
    if (seg.size() == 3) {
        std::vector<int> present;
        for (int k = 0; k < 3; ++k) if (!seg[static_cast<size_t>(k)].empty()) present.push_back(k);
        if (present.size() == parts) {
            for (size_t i = 0; i < parts; ++i) *target[present[i]] = &n.children[i];
            p.cond_is_true = seg[1].empty() || seg[1] == "true" || seg[1] == "1";
            return p;
        }
    }
    for (size_t i = 0; i < parts; ++i) {                                         // fallback on node kinds
        const Node& c = n.children[i];
        if (c.type == "DECL_STMT") p.init = &c;
        else if (c.type == "BINARY_OPERATOR" && isCompare(opOf(c))) p.cond = &c;
        else if (!p.init && !p.cond && c.type == "BINARY_OPERATOR" && opOf(c) == "=") p.init = &c;
        else p.inc = &c;
    }
    p.cond_is_true = !p.cond;
    return p;
}

bool headerIsTrue(const Node& n) {
    const auto seg = headerSegments(n.tokens.value_or(""));
    return seg.size() == 1 && (seg[0] == "true" || seg[0] == "1");
}

// ---------------------------------------------------------------------------------------------------------
// How variables change
// ---------------------------------------------------------------------------------------------------------

enum class Upd { Additive, Geometric, Interval, Other };

using Updates = std::map<std::string, std::set<Upd>>;

bool hasHalving(const Node& n) {
    if (n.type == "BINARY_OPERATOR" && (opOf(n) == "/" || opOf(n) == ">>")) return true;
    for (const Node& c : n.children) if (hasHalving(c)) return true;
    return false;
}

// Names assigned a halved value: `int mid = lo + (hi - lo) / 2;`, `mid = (lo + hi) >> 1;`, `half = n / 2;`
void findHalvingVars(const Node& n, std::set<std::string>& out) {
    if (n.type == "VAR_DECL" && !n.children.empty() && hasHalving(n.children.back()) && !refsOf(n.children.back()).empty())
        out.insert(n.spelling);
    if (n.type == "BINARY_OPERATOR" && opOf(n) == "=" && n.children.size() == 2 && isRef(n.children[0]) && hasHalving(n.children[1]))
        out.insert(refName(n.children[0]));
    for (const Node& c : n.children) findHalvingVars(c, out);
}

bool refsAny(const Node& n, const std::set<std::string>& names) {
    for (const std::string& r : refsOf(n)) if (names.count(r)) return true;
    return false;
}

void collectUpdates(const Node& n, const std::set<std::string>& halving, Updates& out) {
    const std::string& op = opOf(n);
    if (n.type == "UNARY_OPERATOR" && (op == "++" || op == "--") && !n.children.empty() && isRef(n.children[0])) {
        out[refName(n.children[0])].insert(Upd::Additive);
    } else if (n.type == "COMPOUND_ASSIGNMENT_OPERATOR" && n.children.size() == 2 && isRef(n.children[0])) {
        const std::string& v = refName(n.children[0]);
        if (op == "+=" || op == "-=") out[v].insert(refsOf(n.children[1]).count(v) ? Upd::Geometric : Upd::Additive);
        else if (op == "*=" || op == "/=" || op == ">>=" || op == "<<=") out[v].insert(Upd::Geometric);
        else out[v].insert(Upd::Other);
    } else if (n.type == "BINARY_OPERATOR" && op == "=" && n.children.size() == 2 && isRef(n.children[0])) {
        const std::string& v = refName(n.children[0]);
        const Node& rhs = unwrap(n.children[1]);
        if (refsOf(rhs).count(v)) {
            const std::string& rop = opOf(rhs);
            if (rhs.type == "BINARY_OPERATOR" && (rop == "+" || rop == "-")) out[v].insert(Upd::Additive);
            else if (rhs.type == "BINARY_OPERATOR" && (rop == "*" || rop == "/" || rop == ">>" || rop == "<<")) out[v].insert(Upd::Geometric);
            else out[v].insert(Upd::Other);
        } else if (refsAny(rhs, halving)) {
            out[v].insert(Upd::Interval);
        } else {
            out[v].insert(Upd::Other);
        }
    }
    for (const Node& c : n.children) collectUpdates(c, halving, out);
}

// ---------------------------------------------------------------------------------------------------------
// Trip count of one loop
// ---------------------------------------------------------------------------------------------------------

struct Trip {
    TripKind kind = TripKind::Unknown;
    std::string variable;
    std::string reason;
    bool assumed = false;
};

int rank(TripKind k) {
    switch (k) {
        case TripKind::Constant: return 0;
        case TripKind::Logarithmic: return 1;
        case TripKind::Sqrt: return 2;
        case TripKind::Linear: return 3;
        default: return 4;
    }
}

Trip unknown(const std::string& why) { Trip t; t.reason = why; return t; }

Order orderOf(TripKind k) {
    switch (k) {
        case TripKind::Constant: return Order::constant();
        case TripKind::Logarithmic: return Order::logarithmic();
        case TripKind::Sqrt: return Order::sqrtN();
        case TripKind::Linear: return Order::linear();
        default: return Order::unknownOrder();
    }
}

bool hasExit(const Node& n, bool in_breakable) {
    if (n.type == "RETURN_STMT" || n.type == "GOTO_STMT" || n.type == "CXX_THROW_EXPR") return true;
    if (n.type == "BREAK_STMT" && !in_breakable) return true;
    const bool nested = ast::isLoop(n.type) || n.type == "SWITCH_STMT";
    for (const Node& c : n.children) if (hasExit(c, in_breakable || nested)) return true;
    return false;
}

class TripClassifier {
public:
    TripClassifier(const std::set<std::string>& constants, const Updates& updates, const std::map<std::string, bool>& start_const)
        : constants_(constants), updates_(updates), start_const_(start_const) {}

    Trip classify(const Node& raw) const {
        const Node& n = unwrap(raw);
        const std::string& op = opOf(n);

        if (n.type == "BINARY_OPERATOR" && op == "&&") {                          // stops as soon as one side fails
            const Trip a = classify(n.children[0]), b = classify(n.children[1]);
            if (a.kind == TripKind::Unknown) return b;
            if (b.kind == TripKind::Unknown) return a;
            return rank(a.kind) <= rank(b.kind) ? a : b;
        }
        if (n.type == "BINARY_OPERATOR" && op == "||") {                          // runs until both sides fail
            const Trip a = classify(n.children[0]), b = classify(n.children[1]);
            if (a.kind == TripKind::Unknown) return a;
            if (b.kind == TripKind::Unknown) return b;
            return rank(a.kind) >= rank(b.kind) ? a : b;
        }
        if (n.type == "BINARY_OPERATOR" && isCompare(op) && n.children.size() == 2) return compare(n.children[0], n.children[1]);

        if (n.type == "UNARY_OPERATOR" && op == "!" && !n.children.empty()) {
            const Node& inner = unwrap(n.children[0]);
            if (inner.type == "CALL_EXPR" && inner.spelling == "empty") {
                Trip t;
                t.kind = TripKind::Linear;
                t.variable = containerOf(inner);
                t.reason = "drains " + (t.variable.empty() ? std::string("a container") : t.variable) +
                           " (assuming each element is handled a constant number of times)";
                t.assumed = true;
                return t;
            }
            return unknown("the loop ends on a flag or condition I cannot count");
        }
        if (n.type == "UNARY_OPERATOR" && (op == "++" || op == "--") && !n.children.empty() && isRef(n.children[0])) {
            Trip t;
            t.kind = TripKind::Linear;
            t.variable = refName(n.children[0]);
            t.reason = t.variable + " counts " + (op == "--" ? "down" : "up") + " by one until the condition fails";
            return t;
        }
        if (n.type == "DECL_REF_EXPR") {                                           // while (n)
            const auto it = updates_.find(n.spelling);
            if (it != updates_.end()) {
                if (it->second.count(Upd::Additive)) return linear(n.spelling, n.spelling + " changes by a constant each iteration until it reaches 0");
                if (it->second.count(Upd::Geometric)) return logarithmic(n.spelling, n.spelling + " is divided each iteration until it reaches 0");
            }
            return unknown("the condition is a flag whose changes I cannot follow");
        }
        if (n.type == "CXX_BOOL_LITERAL_EXPR" || n.type == "INTEGER_LITERAL") return unknown("the condition is a literal");
        return unknown("the loop condition is a call or expression I cannot count");
    }

private:
    static std::string containerOf(const Node& call) {
        for (const Node& c : call.children) {
            const std::string s = refsOf(c).empty() ? std::string() : *refsOf(c).begin();
            if (!s.empty()) return s;
        }
        return {};
    }

    static Trip linear(const std::string& v, const std::string& why) {
        Trip t; t.kind = TripKind::Linear; t.variable = v; t.reason = why; return t;
    }
    static Trip logarithmic(const std::string& v, const std::string& why) {
        Trip t; t.kind = TripKind::Logarithmic; t.variable = v; t.reason = why; return t;
    }

    Trip compare(const Node& lhs_raw, const Node& rhs_raw) const {
        const Node& lhs = unwrap(lhs_raw);
        const Node& rhs = unwrap(rhs_raw);

        // i * i <= n   or   n >= i * i
        for (int side = 0; side < 2; ++side) {
            const Node& sq = side == 0 ? lhs : rhs;
            const Node& other = side == 0 ? rhs : lhs;
            if (sq.type == "BINARY_OPERATOR" && opOf(sq) == "*" && sq.children.size() == 2 && isRef(sq.children[0]) &&
                isRef(sq.children[1]) && refName(sq.children[0]) == refName(sq.children[1])) {
                const std::string v = refName(sq.children[0]);
                const auto it = updates_.find(v);
                if (it != updates_.end() && it->second.count(Upd::Additive)) {
                    Trip t;
                    t.variable = v;
                    if (isConstantExpr(other, constants_)) { t.kind = TripKind::Constant; t.reason = v + " * " + v + " is compared with a constant"; }
                    else { t.kind = TripKind::Sqrt; t.reason = v + " * " + v + " is compared with the input size, so " + v + " stops near sqrt(n)"; }
                    return t;
                }
            }
        }

        // Every variable of the condition that the loop changes decides how long it runs; the slowest one wins.
        std::set<std::string> names = refsOf(lhs);
        for (const std::string& r : refsOf(rhs)) names.insert(r);
        Trip best;
        bool found = false;
        std::string vars;
        for (const std::string& v : names) {
            const auto it = updates_.find(v);
            if (it == updates_.end()) continue;
            const bool in_lhs = refsOf(lhs).count(v) > 0, in_rhs = refsOf(rhs).count(v) > 0;
            const Node& other = in_lhs && !in_rhs ? rhs : (in_rhs && !in_lhs ? lhs : lhs);
            const bool bound_const = !(in_lhs && in_rhs) && isConstantExpr(other, constants_);
            const bool other_const = bound_const && startsConstant(v);
            const bool from_input = bound_const && !startsConstant(v);
            const bool bound_sqrt = !(in_lhs && in_rhs) && containsCallNamed(other, "sqrt");
            const bool bound_log = !(in_lhs && in_rhs) && (containsCallNamed(other, "log2") || containsCallNamed(other, "log") ||
                                                           containsCallNamed(other, "log10") || containsCallNamed(other, "__lg"));
            Trip t;
            t.variable = v;
            for (Upd u : it->second) {
                Trip c;
                c.variable = v;
                switch (u) {
                    case Upd::Additive:
                        if (other_const) { c.kind = TripKind::Constant; c.reason = v + " steps by a constant up to a constant bound"; }
                        else if (from_input) { c.kind = TripKind::Linear; c.reason = v + " starts from the input and steps by a constant to a fixed bound"; }
                        else if (bound_sqrt) { c.kind = TripKind::Sqrt; c.reason = v + " steps by a constant up to sqrt of the input size"; }
                        else if (bound_log) { c.kind = TripKind::Logarithmic; c.reason = v + " steps by a constant up to the logarithm of the input size"; }
                        else { c.kind = TripKind::Linear; c.reason = v + " steps by a constant up to a bound that grows with the input"; }
                        break;
                    case Upd::Geometric:
                        if (other_const) { c.kind = TripKind::Constant; c.reason = v + " is scaled each iteration but starts and ends at constants"; }
                        else if (from_input) { c.kind = TripKind::Logarithmic; c.reason = v + " starts from the input and is multiplied or divided each iteration, so it takes log n steps"; }
                        else { c.kind = TripKind::Logarithmic; c.reason = v + " is multiplied or divided each iteration, so it reaches the bound in log n steps"; }
                        break;
                    case Upd::Interval:
                        c.kind = TripKind::Logarithmic;
                        c.reason = "the search interval is cut in half each iteration (" + v + " moves to the middle)";
                        break;
                    case Upd::Other:
                        continue;
                }
                if (t.kind == TripKind::Unknown || rank(c.kind) > rank(t.kind)) t = c;     // several updates: the slowest
            }
            if (t.kind == TripKind::Unknown) continue;
            vars += (vars.empty() ? "" : " / ") + v;
            if (!found || rank(t.kind) > rank(best.kind)) best = t;
            found = true;
        }
        if (!found) return unknown("the loop variable is not changed in a way I can follow");
        best.variable = vars;
        return best;
    }

    // True when every assignment to v that the function makes at its declaration or by `v = ...` uses a constant:
    // a loop that starts from a constant and stops at a constant bound is O(1). A parameter, or a variable that
    // starts from the input, makes the same loop depend on n.
    bool startsConstant(const std::string& v) const {
        const auto it = start_const_.find(v);
        return it != start_const_.end() && it->second;
    }

    const std::set<std::string>& constants_;
    const Updates& updates_;
    const std::map<std::string, bool>& start_const_;
};

// ---------------------------------------------------------------------------------------------------------
// The function walk: trip counts, loop costs, recursive calls
// ---------------------------------------------------------------------------------------------------------

class Analyzer {
public:
    Analyzer(const Node& fn, const AnalyzerOptions& opt) : fn_(fn), opt_(opt), constants_(opt.constants.begin(), opt.constants.end()) {
        out_.function = fn.spelling;
        out_.line = fn.line;
        for (const Node& c : fn.children) if (c.type == "PARM_DECL") params_.insert(c.spelling);
        for (const Node* v : ast::collect(fn, "VAR_DECL")) {              // const int N = 100;  inside the function
            if (v->dtype && v->dtype->rfind("const", 0) == 0 && !v->children.empty() && isConstantExpr(v->children.back(), constants_))
                constants_.insert(v->spelling);
        }
        if (const Node* body = ast::bodyOf(fn)) {
            findHalvingVars(*body, halving_);
            findStarts(*body);
        }
    }

    FunctionLoops run() {
        const Node* body = ast::bodyOf(fn_);
        if (!body) return std::move(out_);
        out_.work = cost(*body, -1, 0, Order::constant());
        for (const LoopInfo& l : out_.loops) out_.max_depth = std::max(out_.max_depth, l.depth);
        detectMemo(*body);
        describeLoops();
        return std::move(out_);
    }

private:
    // name -> every initialisation or plain assignment of that name in the function is a constant.
    void findStarts(const Node& n) {
        auto note = [&](const std::string& v, const Node& value) {
            const bool c = isConstantExpr(value, constants_);
            const auto it = start_const_.find(v);
            start_const_[v] = it == start_const_.end() ? c : (it->second && c);
        };
        if (n.type == "VAR_DECL" && !n.children.empty() && !isTypeNode(n.children.back())) note(n.spelling, n.children.back());
        if (n.type == "BINARY_OPERATOR" && opOf(n) == "=" && n.children.size() == 2 && isRef(n.children[0])) note(refName(n.children[0]), n.children[1]);
        for (const Node& c : n.children) findStarts(c);
    }

    static bool isTypeNode(const Node& n) { return n.type == "TYPE_REF" || n.type == "TEMPLATE_REF" || n.type == "NAMESPACE_REF"; }

    // ---- cost of a statement or expression ---------------------------------------------------------------

    Order callCost(const Node& call) const {
        if (!opt_.call_cost) return Order::constant();
        const auto c = opt_.call_cost(call);
        return c ? *c : Order::constant();
    }

    bool isSelfCall(const Node& n) const {
        return n.type == "CALL_EXPR" && !n.spelling.empty() && n.spelling == fn_.spelling && !n.children.empty() &&
               isRef(n.children[0]) && refName(n.children[0]) == fn_.spelling;
    }

    Order costOfChildren(const Node& n, int parent, int depth, const Order& factor, size_t from = 0) {
        Order c = Order::constant();
        for (size_t i = from; i < n.children.size(); ++i) c = dominant(c, cost(n.children[i], parent, depth, factor));
        return c;
    }

    // Runs `f` with a fresh list of recursive calls and returns that list.
    template <class F>
    std::vector<RecursiveCall> collectInto(F&& f, Order& cost_out) {
        std::vector<RecursiveCall> tmp;
        std::swap(tmp, out_.recursive_calls);
        cost_out = f();
        std::swap(tmp, out_.recursive_calls);
        return tmp;
    }

    // Only one branch of an if / ?: runs, so only the branch with more recursive calls is kept.
    Order branches(const std::vector<const Node*>& arms, int parent, int depth, const Order& factor) {
        Order total = Order::constant();
        std::vector<RecursiveCall> keep;
        for (const Node* arm : arms) {
            Order c;
            auto calls = collectInto([&] { return cost(*arm, parent, depth, factor); }, c);
            total = dominant(total, c);
            if (calls.size() > keep.size()) keep = std::move(calls);
        }
        for (auto& r : keep) out_.recursive_calls.push_back(std::move(r));
        return total;
    }

    Order cost(const Node& n, int parent, int depth, const Order& factor) {
        if (opt_.on_node) opt_.on_node(n, depth, factor);
        if (ast::isLoop(n.type)) return loopCost(n, parent, depth, factor);

        if (n.type == "IF_STMT" || n.type == "CONDITIONAL_OPERATOR") {
            // [leading decl?, cond, then, else?]: the last one or two children are the arms
            const size_t arms_n = n.type == "CONDITIONAL_OPERATOR" ? 2 : (n.has_else.value_or(false) ? 2 : 1);
            if (n.children.size() <= arms_n) return costOfChildren(n, parent, depth, factor);
            const size_t first_arm = n.children.size() - arms_n;
            Order c = Order::constant();
            for (size_t i = 0; i < first_arm; ++i) c = dominant(c, cost(n.children[i], parent, depth, factor));
            std::vector<const Node*> arms;
            for (size_t i = first_arm; i < n.children.size(); ++i) arms.push_back(&n.children[i]);
            return dominant(c, branches(arms, parent, depth, factor));
        }

        if (n.type == "CALL_EXPR") {
            if (isSelfCall(n)) {
                recordSelfCall(n, depth, factor);
                Order c = Order::constant();
                for (size_t i = 1; i < n.children.size(); ++i) c = dominant(c, cost(n.children[i], parent, depth, factor));
                return c;
            }
            return dominant(callCost(n), costOfChildren(n, parent, depth, factor));
        }
        return costOfChildren(n, parent, depth, factor);
    }

    Order loopCost(const Node& n, int parent, int depth, const Order& factor) {
        LoopInfo info;
        info.line = n.line;
        info.type = n.type;
        info.parent = parent;
        info.depth = depth + 1;

        const Node* body = nullptr;
        std::vector<const Node*> once, each;               // header parts that run once / every iteration
        const Node* cond = nullptr;
        Updates updates;
        bool cond_true = false;

        if (n.type == "FOR_STMT") {
            const ForParts p = splitFor(n);
            body = p.body;
            cond = p.cond;
            cond_true = p.cond_is_true;
            if (p.init) once.push_back(p.init);
            if (p.cond) each.push_back(p.cond);
            if (p.inc) { each.push_back(p.inc); collectUpdates(*p.inc, halving_, updates); }
        } else if (n.type == "WHILE_STMT") {
            if (n.children.size() >= 2) { cond = &n.children[n.children.size() - 2]; body = &n.children.back(); each.push_back(cond); }
            cond_true = headerIsTrue(n);
        } else if (n.type == "DO_STMT") {
            if (n.children.size() >= 2) { body = &n.children[0]; cond = &n.children[1]; each.push_back(cond); }
            cond_true = cond && unwrap(*cond).type == "CXX_BOOL_LITERAL_EXPR";
        } else {                                           // CXX_FOR_RANGE_STMT: [var, range, body]
            if (!n.children.empty()) body = &n.children.back();
            for (size_t i = 0; i + 1 < n.children.size(); ++i) once.push_back(&n.children[i]);
        }
        if (cond) collectUpdates(*cond, halving_, updates);
        if (body) collectUpdates(*body, halving_, updates);

        info.early_exit = body && hasExit(*body, false);

        Trip trip;
        if (n.type == "CXX_FOR_RANGE_STMT") {
            const Node* range = n.children.size() >= 2 ? &n.children[n.children.size() - 2] : nullptr;
            if (range && unwrap(*range).type == "INIT_LIST_EXPR") { trip.kind = TripKind::Constant; trip.reason = "iterates over a fixed list"; }
            else {
                trip.kind = TripKind::Linear;
                const auto r = range ? refsOf(*range) : std::set<std::string>{};
                trip.variable = r.empty() ? std::string() : *r.begin();
                trip.reason = "visits every element of " + (trip.variable.empty() ? std::string("the range") : trip.variable);
            }
        } else if (cond_true || !cond) {
            if (info.early_exit) trip = unknown("the loop has no condition and ends only through break or return");
            else { trip.kind = TripKind::Infinite; trip.reason = "the condition is always true and nothing breaks out"; }
        } else {
            trip = TripClassifier(constants_, updates, start_const_).classify(*cond);
        }

        info.trip = trip.kind;
        info.trip_order = trip.kind == TripKind::Infinite ? Order::unknownOrder() : orderOf(trip.kind);
        info.variable = trip.variable;
        info.reason = trip.reason;
        info.assumed = trip.assumed;

        const int idx = static_cast<int>(out_.loops.size());
        out_.loops.push_back(info);

        const Order inner_factor = multiply(factor, info.trip_order);
        Order once_cost = Order::constant(), each_cost = Order::constant();
        for (const Node* h : once) once_cost = dominant(once_cost, cost(*h, parent, depth, factor));
        for (const Node* h : each) each_cost = dominant(each_cost, cost(*h, idx, depth + 1, inner_factor));
        const Order body_cost = body ? cost(*body, idx, depth + 1, inner_factor) : Order::constant();

        LoopInfo& stored = out_.loops[static_cast<size_t>(idx)];
        stored.body_cost = dominant(body_cost, each_cost);
        stored.cost = multiply(stored.trip_order, stored.body_cost);
        return dominant(once_cost, stored.cost);
    }

    // ---- recursion ------------------------------------------------------------------------------------------

    std::string show(const Node& raw) const {
        const Node& n = unwrap(raw);
        const std::string& op = opOf(n);
        if (n.type == "DECL_REF_EXPR") return n.spelling;
        if (n.type == "INTEGER_LITERAL") return "c";
        if (n.type == "BINARY_OPERATOR" && n.children.size() == 2) return show(n.children[0]) + " " + (op.empty() ? "?" : op) + " " + show(n.children[1]);
        if (n.type == "UNARY_OPERATOR" && !n.children.empty()) return op + show(n.children[0]);
        return "...";
    }

    // How does this argument show that the problem got smaller?
    bool shrinkOf(const Node& raw, RecursiveCall& rc) const {
        const Node& a = unwrap(raw);
        const std::string& op = opOf(a);
        const auto refs = refsOf(a);
        bool param = false, halved = false;
        for (const std::string& r : refs) { param = param || params_.count(r) > 0; halved = halved || halving_.count(r) > 0; }

        auto divide = [&](const char* why) {
            rc.shrink = RecursiveCall::Shrink::Divide;
            rc.factor = 2.0;
            rc.factor_assumed = true;
            rc.argument = show(a);
            (void)why;
            return true;
        };
        if (a.type == "BINARY_OPERATOR" && a.children.size() == 2) {
            if ((op == "/" || op == ">>" || op == "%") && param) return divide("divides");
            if ((op == "+" || op == "-")) {
                const Node& l = unwrap(a.children[0]);
                const Node& r = unwrap(a.children[1]);
                const bool lit = isLiteralNode(r) || isLiteralNode(l);
                const Node& var = isLiteralNode(r) ? l : r;
                if (lit && isRef(var) && halving_.count(refName(var))) return divide("interval");
                if (lit && isRef(var) && params_.count(refName(var))) {
                    rc.shrink = RecursiveCall::Shrink::Subtract;
                    rc.factor = 1.0;
                    rc.factor_assumed = true;
                    rc.argument = show(a);
                    return true;
                }
            }
            if (hasHalving(a) && param) return divide("inline halving");
        }
        if (a.type == "UNARY_OPERATOR" && (op == "--" || op == "-") && !a.children.empty() && isRef(a.children[0]) && params_.count(refName(a.children[0]))) {
            rc.shrink = RecursiveCall::Shrink::Subtract;
            rc.factor = 1.0;
            rc.factor_assumed = true;
            rc.argument = show(a);
            return true;
        }
        if (isRef(a) && halved && !param) return divide("halving variable");
        return false;
    }

    void recordSelfCall(const Node& call, int depth, const Order& factor) {
        RecursiveCall rc;
        rc.line = call.line;
        rc.loop_depth = depth;
        rc.loop_factor = factor;
        RecursiveCall best = rc;
        bool have = false;
        for (size_t i = 1; i < call.children.size(); ++i) {       // children[0] is the callee
            RecursiveCall cand = rc;
            if (!shrinkOf(call.children[i], cand)) continue;
            if (!have || (cand.shrink == RecursiveCall::Shrink::Divide && best.shrink != RecursiveCall::Shrink::Divide)) { best = cand; have = true; }
        }
        if (!have) {                                              // quicksort: f(lo, p - 1) and f(p + 1, hi) with p a local
            for (size_t i = 1; i < call.children.size() && best.pivot.empty(); ++i) {
                const Node& a = unwrap(call.children[i]);
                if (a.type != "BINARY_OPERATOR" || a.children.size() != 2 || (opOf(a) != "+" && opOf(a) != "-")) continue;
                const Node& l = unwrap(a.children[0]);
                const Node& r = unwrap(a.children[1]);
                const Node& var = isLiteralNode(r) ? l : r;
                if ((isLiteralNode(l) || isLiteralNode(r)) && isRef(var) && !params_.count(refName(var)) && !halving_.count(refName(var))) {
                    best.pivot = refName(var);
                    best.argument = show(a);
                }
            }
        }
        out_.recursive_calls.push_back(best);
    }

    // `if (memo[n] != -1) return memo[n];` at the top of the function: the recursion tree is pruned by a cache.
    void detectMemo(const Node& body) {
        if (out_.recursive_calls.empty()) return;
        for (const Node& s : body.children) {
            if (s.type != "IF_STMT" || s.children.size() < 2) continue;
            const Node& cond = unwrap(s.children[0]);
            bool guard = false;
            if (cond.type == "CALL_EXPR" && (cond.spelling == "count" || cond.spelling == "find" || cond.spelling == "contains")) guard = true;
            if (cond.type == "BINARY_OPERATOR" && isCompare(opOf(cond)) && cond.children.size() == 2) {
                for (int side = 0; side < 2; ++side) {
                    const Node& x = unwrap(cond.children[static_cast<size_t>(side)]);
                    const Node& y = unwrap(cond.children[static_cast<size_t>(1 - side)]);
                    const bool subscript = x.type == "ARRAY_SUBSCRIPT_EXPR" || (x.type == "CALL_EXPR" && x.spelling == "operator[]");
                    const bool literal = isLiteralNode(y) || (y.type == "UNARY_OPERATOR" && !y.children.empty() && isLiteralNode(unwrap(y.children[0])));
                    if (subscript && literal) guard = true;
                }
            }
            if (guard && containsType(s, "RETURN_STMT")) { out_.maybe_memoized = true; return; }
        }
    }

    // ---- derivation text ------------------------------------------------------------------------------------

    static const char* word(const std::string& type) {
        if (type == "FOR_STMT") return "for";
        if (type == "WHILE_STMT") return "while";
        if (type == "DO_STMT") return "do-while";
        return "range-for";
    }

    static std::string times(const LoopInfo& l) {
        switch (l.trip) {
            case TripKind::Constant: return "a constant number of times";
            case TripKind::Logarithmic: return "about log n times";
            case TripKind::Sqrt: return "about sqrt(n) times";
            case TripKind::Linear: return "about n times";
            case TripKind::Infinite: return "forever";
            default: return "an unknown number of times";
        }
    }

    void describeLoops() {
        auto& d = out_.derivation;
        for (const LoopInfo& l : out_.loops) {
            std::string line = std::string(static_cast<size_t>((l.depth - 1) * 2), ' ') + "Line " + std::to_string(l.line) + ": " + word(l.type) +
                               " loop" + (l.variable.empty() ? "" : " on " + l.variable) + " runs " + times(l);
            if (!l.reason.empty()) line += " (" + l.reason + ")";
            if (l.early_exit && l.trip != TripKind::Unknown && l.trip != TripKind::Infinite) line += "; it may stop earlier on a break or return";
            d.push_back(line);
        }
        for (const LoopInfo& l : out_.loops) {
            if (l.parent != -1 || l.depth != 1) continue;
            // A loop nest: show the product that gives its cost.
            const bool nested = std::any_of(out_.loops.begin(), out_.loops.end(), [&](const LoopInfo& o) { return o.parent != -1 && rootOf(o) == &l; });
            if (!nested) continue;
            std::string chain;
            const LoopInfo* cur = &l;
            while (cur) {
                chain += (chain.empty() ? "" : " x ") + orderText(cur->trip_order);
                cur = deepestChild(*cur);
            }
            d.push_back("Loops nest from line " + std::to_string(l.line) + ": " + chain + " = " + l.cost.toString());
        }
        d.push_back("Loop structure of " + (out_.function.empty() ? std::string("the function") : out_.function) + ": " + out_.work.toString());
    }

    static std::string orderText(const Order& o) {
        const std::string s = o.toString();
        return s.size() > 3 ? s.substr(2, s.size() - 3) : s;       // "O(n)" -> "n"
    }

    const LoopInfo* rootOf(const LoopInfo& l) const {
        const LoopInfo* cur = &l;
        while (cur->parent != -1) cur = &out_.loops[static_cast<size_t>(cur->parent)];
        return cur;
    }

    // The child with the highest cost: the one that decides the product.
    const LoopInfo* deepestChild(const LoopInfo& l) const {
        const LoopInfo* best = nullptr;
        const int self = static_cast<int>(&l - out_.loops.data());
        for (const LoopInfo& c : out_.loops)
            if (c.parent == self && (!best || compare(c.cost, best->cost) > 0)) best = &c;
        return best;
    }

    const Node& fn_;
    const AnalyzerOptions& opt_;
    std::set<std::string> constants_;
    std::set<std::string> params_;
    std::set<std::string> halving_;
    std::map<std::string, bool> start_const_;
    FunctionLoops out_;
};

} // namespace

const Node& unwrapExpr(const Node& n) { return unwrap(n); }
bool isConstantExpression(const Node& n, const std::set<std::string>& constants) { return isConstantExpr(n, constants); }
std::set<std::string> referencedNames(const Node& n) { return refsOf(n); }

const char* toString(TripKind k) {
    switch (k) {
        case TripKind::Constant: return "constant";
        case TripKind::Logarithmic: return "logarithmic";
        case TripKind::Sqrt: return "sqrt";
        case TripKind::Linear: return "linear";
        case TripKind::Unknown: return "unknown";
        case TripKind::Infinite: return "infinite";
    }
    return "unknown";
}

FunctionLoops analyzeFunction(const Node& function, const AnalyzerOptions& options) {
    return Analyzer(function, options).run();
}

std::vector<FunctionLoops> analyzeAll(const Node& root, AnalyzerOptions options) {
    for (const Node* v : ast::collect(root, "VAR_DECL"))
        if (v->dtype && v->dtype->rfind("const", 0) == 0 && !v->children.empty() && isConstantExpr(v->children.back(), {}))
            options.constants.push_back(v->spelling);
    std::vector<FunctionLoops> out;
    for (const Node* f : ast::functionsOf(root)) out.push_back(analyzeFunction(*f, options));
    return out;
}

} // namespace nc::complexity
