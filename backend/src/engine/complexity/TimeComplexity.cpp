#include "engine/complexity/TimeComplexity.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

#include "engine/ast/Traversal.hpp"

namespace nc::complexity {

using ast::Node;

// ---------------------------------------------------------------------------------------------------------------------
// Program index
// ---------------------------------------------------------------------------------------------------------------------

ProgramIndex buildIndex(const Node& root) {
    ProgramIndex ix;
    ix.functions = ast::functionsOf(root);
    for (const Node* v : ast::collect(root, "VAR_DECL"))
        if (v->dtype && v->dtype->rfind("const", 0) == 0 && !v->children.empty() && isConstantExpression(v->children.back()))
            ix.constants.insert(v->spelling);
    for (const Node* f : ix.functions) ix.by_name.emplace(f->spelling, f);
    for (const Node* f : ix.functions) {
        auto& out = ix.callees[f->spelling];
        for (const Node* call : ast::collect(*f, "CALL_EXPR"))
            if (call->spelling != f->spelling && ix.by_name.count(call->spelling)) out.insert(call->spelling);
    }
    std::set<std::string> called;
    for (const auto& [caller, set] : ix.callees) for (const std::string& c : set) called.insert(c);
    for (const Node* f : ix.functions)
        if (!called.count(f->spelling) && std::find(ix.roots.begin(), ix.roots.end(), f->spelling) == ix.roots.end()) ix.roots.push_back(f->spelling);
    return ix;
}

std::string chooseEntry(const ProgramIndex& ix, const std::string& hint) {
    if (!hint.empty() && ix.by_name.count(hint)) return hint;
    if (ix.functions.empty()) return {};
    for (auto it = ix.roots.rbegin(); it != ix.roots.rend(); ++it)
        if (*it != "main") return *it;
    if (!ix.roots.empty()) return ix.roots.back();
    return ix.functions.back()->spelling;                      // every function is called by another one (mutual recursion)
}

// ---------------------------------------------------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------------------------------------------------

bool isContainerName(const std::string& n) {
    static const std::set<std::string> names = {"vector", "deque", "list", "string", "basic_string", "unordered_set", "unordered_map",
                                                "unordered_multiset", "unordered_multimap", "set", "map", "multiset", "multimap",
                                                "priority_queue", "queue", "stack", "bitset", "array"};
    return names.count(n) > 0;
}

std::string containerFamily(const std::string& dtype) {
    std::string s = dtype;
    for (const char* q : {"const ", "volatile ", "struct ", "class "}) {
        for (size_t p; (p = s.find(q)) != std::string::npos && p == 0;) s.erase(0, std::string(q).size());
    }
    if (s.rfind("std::", 0) == 0) s.erase(0, 5);
    size_t end = 0;
    while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_')) ++end;
    const std::string head = s.substr(0, end);
    if (head == "basic_string") return "string";
    return isContainerName(head) ? head : std::string();
}

namespace {

bool isTypeNode(const Node& n) { return n.type == "TYPE_REF" || n.type == "TEMPLATE_REF" || n.type == "NAMESPACE_REF"; }

bool isHashFamily(const std::string& f) { return f.rfind("unordered_", 0) == 0; }
bool isTreeFamily(const std::string& f) { return f == "map" || f == "set" || f == "multimap" || f == "multiset"; }

Order logn() { return Order::logarithmic(); }
Order lin() { return Order::linear(); }

// The type of the object a member call or operator call is made on.
const Node* receiverNode(const Node& call) {
    if (call.children.empty()) return nullptr;
    const Node& first = call.children[0];
    if (first.type == "MEMBER_REF_EXPR") return first.children.empty() ? nullptr : &first.children[0];
    if (first.type == "DECL_REF_EXPR" || first.type == "CALL_EXPR" || first.type == "UNEXPOSED_EXPR" || first.type == "MEMBER_REF_EXPR") {
        if (call.spelling.rfind("operator", 0) == 0) return &first;
    }
    return nullptr;
}

std::string receiverFamily(const Node& call) {
    const Node* r = receiverNode(call);
    if (!r) return {};
    const Node& u = unwrapExpr(*r);
    if (u.dtype) {
        const std::string f = containerFamily(*u.dtype);
        if (!f.empty()) return f;
    }
    // adj[u].push_back(v): the receiver is an element access; look at its base container's element type is not
    // available, so fall back to the declared type of the innermost variable
    for (const Node* n = &u; n; n = n->children.empty() ? nullptr : &unwrapExpr(n->children[0]))
        if (n->type == "DECL_REF_EXPR" && n->dtype) return containerFamily(*n->dtype);
    return {};
}

bool argIsChar(const Node& call, size_t i) {
    if (i >= call.children.size()) return false;
    const Node& a = unwrapExpr(call.children[i]);
    const std::string t = a.dtype.value_or("");
    return t == "char" || t == "const char" || a.type == "CHARACTER_LITERAL";
}

} // namespace

Order constructionSize(const Node& call, const std::set<std::string>& constants) {
    std::vector<const Node*> args;
    for (const Node& c : call.children) {
        if (isTypeNode(c)) continue;
        const std::string t = c.dtype.value_or("");
        if (t.find("allocator") != std::string::npos || t.find("less") != std::string::npos || t.find("hash") != std::string::npos ||
            t.find("equal_to") != std::string::npos || t.find("greater") != std::string::npos)
            continue;
        args.push_back(&c);
    }
    if (args.empty()) return Order::constant();
    const Node& first = unwrapExpr(*args[0]);
    if (first.type == "INIT_LIST_EXPR") return Order::constant();
    Order outer = isConstantExpression(first, constants) ? Order::constant() : Order::linear();
    // vector<vector<int>> dp(n, vector<int>(m)): the inner constructor's size multiplies the outer one
    for (size_t i = 1; i < args.size(); ++i) {
        const std::function<const Node*(const Node&)> findCtor = [&](const Node& n) -> const Node* {
            if (n.type == "CALL_EXPR" && isContainerName(n.spelling)) return &n;
            for (const Node& c : n.children) if (const Node* r = findCtor(c)) return r;
            return nullptr;
        };
        if (const Node* inner = findCtor(*args[i])) return multiply(outer, constructionSize(*inner, constants));
    }
    return outer;
}

// ---------------------------------------------------------------------------------------------------------------------
// Library cost table
// ---------------------------------------------------------------------------------------------------------------------

std::optional<Order> libraryCallCost(const Node& call, const std::set<std::string>& constants) {
    const std::string& name = call.spelling;
    if (name.empty()) return std::nullopt;

    if (isContainerName(name)) {                                           // constructor call
        const Order size = constructionSize(call, constants);
        return size.isConstant() ? std::nullopt : std::optional<Order>(size);
    }

    const std::string fam = receiverFamily(call);
    const bool member = !call.children.empty() && call.children[0].type == "MEMBER_REF_EXPR";
    const bool op = name.rfind("operator", 0) == 0;

    // ---- member functions and operators, by container -------------------------------------------------------
    if (member || (op && !fam.empty())) {
        if (name == "find" || name == "count" || name == "lower_bound" || name == "upper_bound" || name == "equal_range" || name == "erase" ||
            name == "insert" || name == "emplace" || name == "contains" || name == "at" || name == "operator[]" || name == "extract") {
            if (isHashFamily(fam)) return std::nullopt;                      // O(1) on average
            if (isTreeFamily(fam)) return logn();
            if (fam == "string" && (name == "find")) return lin();
            if ((fam == "vector" || fam == "deque" || fam == "string") && (name == "erase" || name == "insert" || name == "emplace")) return lin();
            if (fam == "bitset" && name == "count") return lin();
            return std::nullopt;                                             // operator[] / at on sequences, list erase, ...
        }
        if (fam == "priority_queue" && (name == "push" || name == "pop" || name == "emplace")) return logn();
        if (name == "clear" || name == "resize" || name == "assign" || name == "reserve" || name == "shrink_to_fit") {
            return fam == "list" || fam == "deque" || !fam.empty() ? std::optional<Order>(lin()) : std::nullopt;
        }
        if (name == "substr" || name == "append" || name == "operator+" || name == "replace" || name == "compare" || name == "rfind" ||
            name == "find_first_of" || name == "find_last_of" || name == "find_first_not_of" || name == "to_string")
            return fam == "string" ? std::optional<Order>(lin()) : std::nullopt;
        if (name == "operator+=" && fam == "string") return argIsChar(call, call.children.size() - 1) ? std::nullopt : std::optional<Order>(lin());
        if ((name == "operator==" || name == "operator!=" || name == "operator<" || name == "operator>" || name == "operator<=" ||
             name == "operator>=") && (fam == "string" || fam == "vector" || fam == "deque" || fam == "list"))
            return lin();
        if (name == "sort" && fam == "list") return Order::linearithmic();
        if ((name == "reverse" || name == "unique" || name == "remove" || name == "merge") && fam == "list") return lin();
        if (name == "operator=" && !fam.empty() && fam != "priority_queue") return lin();      // copy assignment
        return std::nullopt;                                                 // push_back, size, empty, begin, ... O(1)
    }

    // ---- free functions (the standard algorithms) -----------------------------------------------------------
    static const std::set<std::string> nlogn = {"sort", "stable_sort", "partial_sort", "sort_heap", "partial_sort_copy"};
    static const std::set<std::string> linear = {
        "find", "find_if", "find_if_not", "count", "count_if", "accumulate", "reduce", "transform_reduce", "max_element", "min_element",
        "minmax_element", "reverse", "fill", "fill_n", "iota", "copy", "copy_if", "copy_n", "unique", "unique_copy", "remove", "remove_if",
        "replace", "replace_if", "rotate", "equal", "mismatch", "all_of", "any_of", "none_of", "for_each", "transform", "adjacent_difference",
        "partial_sum", "inner_product", "merge", "inplace_merge", "next_permutation", "prev_permutation", "is_sorted", "shuffle",
        "random_shuffle", "lexicographical_compare", "search", "swap_ranges", "memset", "memcpy", "memmove", "memcmp", "strlen", "strcmp",
        "strcpy", "nth_element", "make_heap", "partition", "stable_partition", "set_union", "set_intersection", "set_difference",
        "is_permutation", "generate", "exclusive_scan", "inclusive_scan", "min_element", "stoi", "stol", "stoll"};
    static const std::set<std::string> logarithmic = {"binary_search", "lower_bound", "upper_bound", "equal_range", "push_heap", "pop_heap", "gcd", "__gcd", "lcm"};
    if (nlogn.count(name)) return Order::linearithmic();
    if (linear.count(name)) return (name == "stoi" || name == "stol" || name == "stoll") ? std::nullopt : std::optional<Order>(lin());
    if (logarithmic.count(name)) return logn();
    if (op && (name == "operator+") && !call.children.empty()) {          // string concatenation: a + b
        const std::string t = call.dtype.value_or("");
        if (containerFamily(t) == "string") return lin();
    }
    if ((name == "operator==" || name == "operator!=" || name == "operator<" || name == "operator>") && !call.children.empty()) {
        const Node& a = unwrapExpr(call.children[0]);
        const std::string f = containerFamily(a.dtype.value_or(""));
        if (f == "string" || f == "vector" || f == "deque") return lin();
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------------
// Time analysis
// ---------------------------------------------------------------------------------------------------------------------

namespace {

bool isIntegerType(const std::string& t) {
    static const std::set<std::string> ints = {"int", "long", "long long", "unsigned", "unsigned int", "unsigned long", "unsigned long long",
                                                "size_t", "short", "int64_t", "int32_t", "uint64_t", "uint32_t", "long int", "long long int"};
    return ints.count(t) > 0;
}

class TimeEngine {
public:
    TimeEngine(const ProgramIndex& ix, TimeAnalysis& out) : ix_(ix), out_(out) {}

    const FunctionTime& resolve(const std::string& name) {
        if (const auto it = done_.find(name); it != done_.end()) return it->second;
        if (in_progress_.count(name)) {
            note("mutual recursion between " + name + " and its callers is not analysed");
            cycle_ = FunctionTime{};
            cycle_.name = name;
            cycle_.cost = Order::unknownOrder();
            cycle_.method = "unsolved";
            return cycle_;
        }
        in_progress_.insert(name);
        FunctionTime ft = analyse(*ix_.by_name.at(name));
        in_progress_.erase(name);
        return done_.emplace(name, std::move(ft)).first->second;
    }

private:
    void note(const std::string& s) {
        if (std::find(out_.assumptions.begin(), out_.assumptions.end(), s) == out_.assumptions.end()) out_.assumptions.push_back(s);
    }

    FunctionTime analyse(const Node& fn) {
        FunctionTime ft;
        ft.name = fn.spelling;
        ft.line = fn.line;

        std::vector<std::string> call_lines;
        AnalyzerOptions opt;
        opt.constants.assign(ix_.constants.begin(), ix_.constants.end());
        opt.call_cost = [&](const Node& call) -> std::optional<Order> {
            std::optional<Order> c;
            const bool user = call.spelling != fn.spelling && ix_.by_name.count(call.spelling) > 0;
            if (user) c = resolve(call.spelling).cost;
            else {
                c = libraryCallCost(call, ix_.constants);
                if (!c && !isKnownConstant(call) && call.spelling != fn.spelling) unknownCallee_.insert(call.spelling);
            }
            if (c && !c->isConstant()) call_lines.push_back("Line " + std::to_string(call.line) + ": " + call.spelling + (user ? "()" : "") +
                                                          " costs " + c->toString());
            return c;
        };
        const FunctionLoops fl = analyzeFunction(fn, opt);

        for (const std::string& d : fl.derivation) if (d.rfind("Loop structure", 0) != 0) ft.derivation.push_back(d);
        std::sort(call_lines.begin(), call_lines.end());
        call_lines.erase(std::unique(call_lines.begin(), call_lines.end()), call_lines.end());
        for (const std::string& d : call_lines) ft.derivation.push_back(d);

        if (fl.recursive_calls.empty()) {
            ft.cost = fl.work;
            ft.method = "loops";
            ft.derivation.push_back(ft.name + " does " + fl.work.toString() + " work per call");
            return ft;
        }
        ft.recursive = true;
        solveRecursion(fn, fl, ft);
        return ft;
    }

    bool isKnownConstant(const Node& call) const {
        static const std::set<std::string> known = {
            "size", "length", "empty", "push_back", "pop_back", "emplace_back", "front", "back", "begin", "end", "rbegin", "rend", "cbegin",
            "cend", "at", "push", "pop", "top", "push_front", "pop_front", "min", "max", "abs", "swap", "make_pair", "make_tuple", "get",
            "sqrt", "pow", "floor", "ceil", "round", "log", "log2", "log10", "exp", "sin", "cos", "fabs", "labs", "llabs", "to_string", "stoi",
            "stoll", "stol", "data", "first", "second", "move", "forward", "tie", "ref", "hypot", "atan2", "__builtin_popcount", "__lg",
            "popcount", "clz", "ctz", "assert", "printf", "scanf", "puts", "putchar", "getchar", "rand", "srand", "time", "clock", "isdigit",
            "isalpha", "isalnum", "islower", "isupper", "tolower", "toupper", "isspace", "static_cast", "max_size", "capacity", "operator()",
            "operator*", "operator->", "operator++", "operator--", "operator<<", "operator>>", "fabsl", "nth", "next", "prev", "distance",
            "advance", "numeric_limits", "min_element_unused"};
        if (known.count(call.spelling)) return true;
        if (call.spelling.rfind("operator", 0) == 0) return true;
        if (isContainerName(call.spelling)) return true;
        if (!call.children.empty() && call.children[0].type == "MEMBER_REF_EXPR") return true;       // member calls are in the table
        return false;
    }

    void solveRecursion(const Node& fn, const FunctionLoops& fl, FunctionTime& ft) {
        const std::string who = ft.name;
        auto unsolved = [&](const std::string& why) {
            ft.cost = Order::unknownOrder();
            ft.method = "unsolved";
            ft.derivation.push_back(who + " is recursive, but " + why);
        };

        bool in_loop = false, any_unknown = false;
        std::string pivot;
        for (const RecursiveCall& rc : fl.recursive_calls) {
            in_loop = in_loop || rc.loop_depth > 0;
            if (rc.shrink == RecursiveCall::Shrink::Unknown) { any_unknown = true; if (pivot.empty()) pivot = rc.pivot; }
        }

        if (fl.maybe_memoized) {                                      // n^k distinct states, each does the non-recursive work once
            int k = 0;
            for (const Node& p : fn.children) if (p.type == "PARM_DECL" && p.dtype && isIntegerType(*p.dtype)) ++k;
            k = std::max(k, 1);
            ft.cost = multiply(Order::polynomial(k), fl.work);
            ft.method = "memoised states";
            note("results of " + who + " look cached, so each of about n^" + std::to_string(k) + " states is computed once");
            ft.derivation.push_back(who + " caches its results (the early return on a stored value): about n^" + std::to_string(k) +
                                    " distinct states (one per integer parameter), each costing " + fl.work.toString() + " -> " + ft.cost.toString());
            return;
        }
        if (in_loop) {
            unsolved("a call sits inside a loop, so each call starts several more; the cost depends on how much of the input they share (a visited set, for instance)");
            return;
        }
        if (any_unknown) {
            // Quicksort shape: both halves come from a pivot p: f(lo, p - 1) and f(p + 1, hi).
            int minus = 0, plus = 0;
            bool all_pivot = !pivot.empty() && fl.recursive_calls.size() == 2;
            for (const RecursiveCall& rc : fl.recursive_calls) {
                if (rc.pivot != pivot) all_pivot = false;
                if (rc.argument.find(" - ") != std::string::npos) ++minus;
                if (rc.argument.find(" + ") != std::string::npos) ++plus;
            }
            if (all_pivot && minus == 1 && plus == 1) {
                ft.cost = multiply(Order::linear(), fl.work);
                ft.method = "pivot split";
                note("the pivot split in " + who + " is charged its worst case (a pivot that is always the smallest or largest element)");
                ft.derivation.push_back(who + " splits around a pivot (" + pivot + "): T(n) = T(k) + T(n-k-1) + " + fl.work.toString());
                ft.derivation.push_back("worst case, an unbalanced pivot: T(n) = T(n-1) + " + fl.work.toString() + " -> " + ft.cost.toString());
                if (fl.work.n_exp == 1.0 && fl.work.log_exp == 0 && !fl.work.exponential)
                    ft.derivation.push_back("with balanced pivots this is O(n log n), which is the average case");
                return;
            }
            unsolved("I cannot tell how its arguments shrink");
            return;
        }

        Recurrence r;
        r.work = fl.work;
        for (const RecursiveCall& rc : fl.recursive_calls) {
            SubProblem s;
            s.kind = rc.shrink == RecursiveCall::Shrink::Divide ? SubProblem::Kind::Divide : SubProblem::Kind::Subtract;
            s.factor = rc.factor;
            s.count = 1;
            r.calls.push_back(s);
            if (rc.factor_assumed)
                note(std::string("line ") + std::to_string(rc.line) + ": the " + (s.kind == SubProblem::Kind::Divide ? "divisor" : "decrement") +
                     " in the recursive call is assumed to be " + (s.kind == SubProblem::Kind::Divide ? "2" : "1"));
        }
        const Solution sol = solveRecurrence(r);
        ft.cost = sol.order;
        ft.method = sol.method;
        for (const std::string& d : sol.steps) ft.derivation.push_back(d);
        if (!sol.ok) note(who + ": " + (sol.steps.size() > 1 ? sol.steps[1] : std::string("the recurrence could not be solved")));
    }

    const ProgramIndex& ix_;
    TimeAnalysis& out_;
    std::map<std::string, FunctionTime> done_;
    std::set<std::string> in_progress_;
    std::set<std::string> unknownCallee_;
    FunctionTime cycle_;

public:
    const std::set<std::string>& unknownCallees() const { return unknownCallee_; }
};

} // namespace

TimeAnalysis analyzeTime(const Node& root, const TimeOptions& options) {
    TimeAnalysis out;
    const ProgramIndex ix = buildIndex(root);
    out.entry = chooseEntry(ix, options.entry);
    if (out.entry.empty()) {
        out.derivation.push_back("No function with a body was found.");
        return out;
    }

    TimeEngine engine(ix, out);
    for (const Node* f : ix.functions) {                       // source order, every function once
        if (std::any_of(out.functions.begin(), out.functions.end(), [&](const FunctionTime& x) { return x.name == f->spelling; })) continue;
        out.functions.push_back(engine.resolve(f->spelling));
    }
    const FunctionTime& entry = engine.resolve(out.entry);
    out.time = entry.cost;
    out.derivation = entry.derivation;
    for (const std::string& callee : engine.unknownCallees())
        out.assumptions.push_back(callee + " is not defined here and not in the cost table: assumed O(1)");
    out.derivation.push_back("Total running time of " + out.entry + ": " + out.time.toString());
    out.confident = !out.time.unknown && out.assumptions.empty();
    return out;
}

} // namespace nc::complexity
