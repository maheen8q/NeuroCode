#include "engine/complexity/SpaceComplexity.hpp"

#include <algorithm>
#include <cctype>

#include "engine/ast/Traversal.hpp"

namespace nc::complexity {

using ast::Node;

namespace {

constexpr long kSmallDimension = 256;      // 26 letters, 128 ASCII codes, 256 byte values

std::string orderText(const Order& o) {
    const std::string s = o.toString();
    return s.size() > 3 ? s.substr(2, s.size() - 3) : s;           // "O(n)" -> "n"
}

std::string elementsText(const Order& o) {
    return o.isConstant() ? std::string("a constant number of elements") : orderText(o) + " elements";
}

bool isTypeNode(const Node& n) { return n.type == "TYPE_REF" || n.type == "TEMPLATE_REF" || n.type == "NAMESPACE_REF"; }

// a / b for orders built as products of n^k log^j: the factor of `a` that `b` does not account for.
Order relative(const Order& a, const Order& b) {
    if (a.unknown || b.unknown || a.exponential || b.exponential) return a;
    Order r = a;
    r.n_exp = std::max(0.0, a.n_exp - b.n_exp);
    r.log_exp = std::max(0, a.log_exp - b.log_exp);
    return r;
}

const Node* findFirst(const Node& n, const std::string& type) {
    if (n.type == type) return &n;
    for (const Node& c : n.children) if (const Node* r = findFirst(c, type)) return r;
    return nullptr;
}

const Node* findCtor(const Node& n) {
    if (n.type == "CALL_EXPR" && isContainerName(n.spelling)) return &n;
    for (const Node& c : n.children) if (const Node* r = findCtor(c)) return r;
    return nullptr;
}

// The variable a member or operator call works on: `adj[u].push_back(v)` -> adj.
std::string rootVariable(const Node& call) {
    if (call.children.empty()) return {};
    const Node* n = &call.children[0];
    while (n) {
        const Node& u = unwrapExpr(*n);
        if (u.type == "DECL_REF_EXPR") return u.spelling;
        n = u.children.empty() ? nullptr : &u.children[0];
    }
    return {};
}

std::string receiverFamilyOf(const Node& call) {
    if (call.children.empty()) return {};
    const Node* n = &call.children[0];
    std::string fam;
    while (n) {
        const Node& u = unwrapExpr(*n);
        if (u.type == "DECL_REF_EXPR" && u.dtype) { fam = containerFamily(*u.dtype); break; }
        if (u.dtype && fam.empty()) fam = containerFamily(*u.dtype);
        n = u.children.empty() ? nullptr : &u.children[0];
    }
    return fam;
}

// "int[1005][1005]" -> dimension texts {"1005", "1005"}
std::vector<std::string> arrayDims(const std::string& dtype) {
    std::vector<std::string> dims;
    for (size_t i = 0; i < dtype.size(); ++i) {
        if (dtype[i] != '[') continue;
        const size_t j = dtype.find(']', i);
        if (j == std::string::npos) break;
        dims.push_back(dtype.substr(i + 1, j - i - 1));
        i = j;
    }
    return dims;
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// Elements of a fixed-size array from its type text; `assumed` when a big constant stands for the input size.
Order arrayElements(const std::string& dtype, bool& assumed) {
    Order o = Order::constant();
    for (const std::string& d : arrayDims(dtype)) {
        if (allDigits(d)) {
            if (d.size() > 4 || std::stol(d) > kSmallDimension) { o = multiply(o, Order::linear()); assumed = true; }
        } else {
            o = multiply(o, Order::linear());                          // a variable length array: int a[n]
        }
    }
    return o;
}

bool isArrayNew(const Node& n) {
    if (n.children.empty()) return false;
    const std::string t = n.children[0].dtype.value_or("");
    return t == "unsigned long" || t == "size_t" || t == "unsigned long long";
}

// `new T` is one element, `new T[5]` is a constant (the literal's value is not in the AST, so a literal counts as small),
// `new T[n]` is n.
Order newElements(const Node& n, const std::set<std::string>& constants) {
    if (!isArrayNew(n)) return Order::constant();
    return isConstantExpression(unwrapExpr(n.children[0]), constants) ? Order::constant() : Order::linear();
}

// One declaration: a container, an array or a pointer to new memory. nullopt for scalars and everything else.
std::optional<Allocation> classify(const Node& var, const std::set<std::string>& constants) {
    const std::string dtype = var.dtype.value_or("");
    Allocation a;
    a.name = var.spelling;
    a.line = var.line;
    if (!dtype.empty() && dtype.back() == ']') {
        a.kind = "array";
        a.elements = arrayElements(dtype, a.assumed);
        a.text = "Line " + std::to_string(var.line) + ": array " + var.spelling + " (" + dtype + ") holds " + elementsText(a.elements) +
                 (a.assumed ? " (a large fixed size is assumed to be sized for the input)" : "");
        return a;
    }
    if (const std::string fam = containerFamily(dtype); !fam.empty()) {
        a.kind = fam;
        a.elements = Order::constant();
        for (const Node& c : var.children) {
            if (isTypeNode(c)) continue;
            if (const Node* ctor = findCtor(c)) { a.elements = constructionSize(*ctor, constants); break; }
            // initialised from something that is not a constructor call: the result of a function or a copy
            if (c.type != "INIT_LIST_EXPR" && findFirst(c, "CALL_EXPR")) { a.elements = Order::linear(); a.assumed = true; break; }
        }
        a.text = "Line " + std::to_string(var.line) + ": " + fam + " " + var.spelling + " starts with " + elementsText(a.elements);
        return a;
    }
    if (!dtype.empty() && dtype.back() == '*') {
        if (const Node* nw = findFirst(var, "CXX_NEW_EXPR")) {
            a.kind = "new";
            a.elements = newElements(*nw, constants);
            a.text = "Line " + std::to_string(var.line) + ": " + var.spelling + " points to new memory with " + elementsText(a.elements);
            return a;
        }
    }
    return std::nullopt;
}

std::optional<Order> librarySpace(const Node& call) {
    static const std::set<std::string> buffers = {"stable_sort", "inplace_merge", "merge", "stable_partition", "partial_sort_copy"};
    if (buffers.count(call.spelling)) return Order::linear();
    if ((call.spelling == "substr" || call.spelling == "to_string") && receiverFamilyOf(call) == "string") return Order::linear();
    if (call.spelling == "substr") return Order::linear();
    if (call.spelling == "operator+" && containerFamily(call.dtype.value_or("")) == "string") return Order::linear();
    if (call.spelling == "malloc" || call.spelling == "calloc") return Order::linear();
    return std::nullopt;
}

class SpaceEngine {
public:
    SpaceEngine(const ProgramIndex& ix, SpaceAnalysis& out) : ix_(ix), out_(out) {}

    const FunctionSpace& resolve(const std::string& name) {
        if (const auto it = done_.find(name); it != done_.end()) return it->second;
        if (in_progress_.count(name)) {
            note("mutual recursion between " + name + " and its callers is not analysed");
            cycle_ = FunctionSpace{};
            cycle_.name = name;
            cycle_.total = Order::unknownOrder();
            return cycle_;
        }
        in_progress_.insert(name);
        FunctionSpace fs = analyse(*ix_.by_name.at(name));
        in_progress_.erase(name);
        return done_.emplace(name, std::move(fs)).first->second;
    }

    void note(const std::string& s) {
        if (std::find(out_.assumptions.begin(), out_.assumptions.end(), s) == out_.assumptions.end()) out_.assumptions.push_back(s);
    }

private:
    struct Local {
        Allocation alloc;
        Order decl_factor;
        Order growth;
    };

    FunctionSpace analyse(const Node& fn) {
        FunctionSpace fs;
        fs.name = fn.spelling;
        fs.line = fn.line;

        std::map<std::string, Local> locals;
        std::vector<Allocation> extra;                 // news, temporaries, copies
        std::vector<std::string> callee_lines;
        Order callees = Order::constant();

        // Parameters passed by value are copies of the input.
        for (const Node& p : fn.children) {
            if (p.type != "PARM_DECL" || !p.dtype) continue;
            const std::string& t = *p.dtype;
            if (containerFamily(t).empty() || t.find('&') != std::string::npos || t.find('*') != std::string::npos) continue;
            Allocation a;
            a.name = p.spelling;
            a.line = fn.line;
            a.kind = "copy";
            a.elements = Order::linear();
            a.text = "Parameter " + p.spelling + " (" + t + ") is passed by value, so the call copies it: n elements";
            extra.push_back(a);
        }

        AnalyzerOptions opt;
        opt.constants.assign(ix_.constants.begin(), ix_.constants.end());
        opt.on_node = [&](const Node& n, int, const Order& factor) {
            if (n.type == "VAR_DECL") {
                if (auto a = classify(n, ix_.constants)) locals[n.spelling] = Local{*a, factor, Order::constant()};
                if (const Node* nw = findFirst(n, "CXX_NEW_EXPR")) declared_news_.insert(nw);
            } else if (n.type == "CXX_NEW_EXPR" && !declared_news_.count(&n)) {          // stored into a field or container: it stays
                Allocation a;
                a.name = "new";
                a.line = n.line;
                a.kind = "new";
                const Order each = newElements(n, ix_.constants);
                a.elements = multiply(each, factor);
                a.text = "Line " + std::to_string(n.line) + ": new inside code that runs " + orderText(factor) + " times keeps " + elementsText(a.elements);
                extra.push_back(a);
            } else if (n.type == "CALL_EXPR") {
                const std::string& name = n.spelling;
                const bool user = name != fn.spelling && ix_.by_name.count(name) > 0;
                if (user) {
                    const Order c = resolve(name).total;
                    callees = dominant(callees, c);
                    if (!c.isConstant()) callee_lines.push_back("Line " + std::to_string(n.line) + ": " + name + "() needs " + c.toString() + " while it runs");
                } else if (const auto s = librarySpace(n)) {
                    callees = dominant(callees, *s);
                    callee_lines.push_back("Line " + std::to_string(n.line) + ": " + name + " uses " + s->toString() + " extra memory");
                }
                static const std::set<std::string> grows = {"push_back", "emplace_back", "push", "emplace", "insert", "push_front", "emplace_front",
                                                           "append", "operator+=", "resize", "assign"};
                const std::string var = rootVariable(n);
                const auto it = locals.find(var);
                if (it == locals.end()) return;
                const std::string fam = receiverFamilyOf(n);
                const bool maplike = (fam == "map" || fam == "unordered_map" || fam == "multimap" || fam == "unordered_multimap") && name == "operator[]";
                if (!grows.count(name) && !maplike) return;
                if (name == "resize" || name == "assign") {
                    const Node* last = n.children.size() > 1 ? &n.children[1] : nullptr;
                    if (last && isConstantExpression(*last, ix_.constants)) return;
                    it->second.growth = dominant(it->second.growth, Order::linear());
                    return;
                }
                // an append inside loops that run `factor` times adds that many elements; loops that surround the declaration
                // too are not counted: the container is rebuilt on every pass
                it->second.growth = dominant(it->second.growth, relative(factor, it->second.decl_factor));
            }
        };
        const FunctionLoops fl = analyzeFunction(fn, opt);

        Order locals_order = Order::constant();
        for (auto& [name, l] : locals) {
            Allocation a = l.alloc;
            a.elements = dominant(a.elements, l.growth);
            if (!l.growth.isConstant()) a.text += "; appends in loops grow it to " + elementsText(l.growth);
            a.text += " -> " + a.elements.toString();
            fs.allocations.push_back(a);
            locals_order = dominant(locals_order, a.elements);
        }
        for (Allocation& a : extra) {
            if (a.text.find("->") == std::string::npos) a.text += " -> " + a.elements.toString();
            fs.allocations.push_back(a);
            locals_order = dominant(locals_order, a.elements);
            if (a.assumed) note("line " + std::to_string(a.line) + ": a large fixed-size block is assumed to be sized for the input");
        }
        std::sort(fs.allocations.begin(), fs.allocations.end(), [](const Allocation& x, const Allocation& y) { return x.line < y.line; });
        for (const Allocation& a : fs.allocations) {
            if (a.assumed) note("line " + std::to_string(a.line) + ": a large fixed-size array is assumed to be sized for the input");
            fs.derivation.push_back(a.text);
        }
        std::sort(callee_lines.begin(), callee_lines.end());
        callee_lines.erase(std::unique(callee_lines.begin(), callee_lines.end()), callee_lines.end());
        for (const std::string& l : callee_lines) fs.derivation.push_back(l);

        fs.locals = locals_order;
        fs.callees = callees;
        fs.depth = Order::constant();
        const Order frame = dominant(locals_order, callees);
        fs.total = frame;

        if (!fl.recursive_calls.empty()) {
            fs.recursive = true;
            bool all_divide = true, in_loop = false, unknown_shape = false;
            for (const RecursiveCall& rc : fl.recursive_calls) {
                if (rc.shrink != RecursiveCall::Shrink::Divide) all_divide = false;
                if (rc.shrink == RecursiveCall::Shrink::Unknown && rc.pivot.empty()) unknown_shape = true;
                in_loop = in_loop || rc.loop_depth > 0;
            }
            if (in_loop) note(fs.name + ": a recursive call inside a loop, so the recursion depth is assumed to be n");
            if (unknown_shape) note(fs.name + ": I cannot tell how its arguments shrink, so the recursion depth is assumed to be n");
            const bool halving = all_divide && !in_loop;
            fs.depth = halving ? Order::logarithmic() : Order::linear();
            if (halving && frame.n_exp > 0.0 && !frame.exponential) fs.total = frame;       // the frames shrink geometrically: the top one dominates
            else fs.total = multiply(fs.depth, frame);
            fs.derivation.push_back(fs.name + " recurses to depth " + orderText(fs.depth) + ", and every active call holds its own frame of " +
                                    frame.toString() + (halving && frame.n_exp > 0.0 ? "; frames of halving sizes add up to the first one" : "") + " -> " +
                                    fs.total.toString());
        }
        if (fs.allocations.empty() && callee_lines.empty() && !fs.recursive)
            fs.derivation.push_back(fs.name + " allocates nothing: a constant number of scalar variables -> O(1)");
        fs.derivation.push_back("Space of " + fs.name + ": " + fs.total.toString());
        return fs;
    }

    const ProgramIndex& ix_;
    SpaceAnalysis& out_;
    std::map<std::string, FunctionSpace> done_;
    std::set<std::string> in_progress_;
    std::set<const Node*> declared_news_;
    FunctionSpace cycle_;
};

} // namespace

SpaceAnalysis analyzeSpace(const Node& root, const SpaceOptions& options) {
    SpaceAnalysis out;
    const ProgramIndex ix = buildIndex(root);
    out.entry = chooseEntry(ix, options.entry);
    if (out.entry.empty()) {
        out.derivation.push_back("No function with a body was found.");
        return out;
    }
    SpaceEngine engine(ix, out);
    for (const Node* f : ix.functions) {
        if (std::any_of(out.functions.begin(), out.functions.end(), [&](const FunctionSpace& x) { return x.name == f->spelling; })) continue;
        out.functions.push_back(engine.resolve(f->spelling));
    }
    const FunctionSpace& entry = engine.resolve(out.entry);
    out.space = entry.total;
    out.derivation = entry.derivation;

    // Global arrays and containers are live for the whole run.
    for (const Node& g : root.children) {
        if (g.type != "VAR_DECL") continue;
        if (auto a = classify(g, ix.constants)) {
            a->kind = "global " + a->kind;
            a->text += " -> " + a->elements.toString();
            out.derivation.insert(out.derivation.begin(), a->text);
            if (a->assumed) engine.note("line " + std::to_string(a->line) + ": a large fixed-size array is assumed to be sized for the input");
            out.space = dominant(out.space, a->elements);
        }
    }
    out.derivation.push_back("Auxiliary space of " + out.entry + ": " + out.space.toString());
    out.confident = !out.space.unknown && out.assumptions.empty();
    return out;
}

Complexity analyzeComplexity(const Node& root, const std::string& entry) {
    TimeOptions to;
    to.entry = entry;
    SpaceOptions so;
    so.entry = entry;
    const TimeAnalysis t = analyzeTime(root, to);
    const SpaceAnalysis s = analyzeSpace(root, so);

    Complexity c;
    c.time = t.time.unknown ? "unknown" : t.time.toString();
    c.space = s.space.unknown ? "unknown" : s.space.toString();
    c.derivation.push_back("Time:");
    for (const std::string& d : t.derivation) c.derivation.push_back(d);
    c.derivation.push_back("Space:");
    for (const std::string& d : s.derivation) c.derivation.push_back(d);
    std::vector<std::string> seen;
    for (const auto* list : {&t.assumptions, &s.assumptions})
        for (const std::string& a : *list)
            if (std::find(seen.begin(), seen.end(), a) == seen.end()) { seen.push_back(a); c.derivation.push_back("Assumption: " + a); }
    return c;
}

} // namespace nc::complexity
