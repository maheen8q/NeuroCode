#include "engine/checks/rules/MemoryRules.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>

#include "engine/ast/Traversal.hpp"

namespace nc::checks {

using ast::Node;

namespace {

const std::string& opOf(const Node& n) {
    static const std::string empty;
    return n.op ? *n.op : empty;
}

bool isSubscript(const Node& n) {
    return (n.type == "ARRAY_SUBSCRIPT_EXPR" || (n.type == "CALL_EXPR" && n.spelling == "operator[]")) && n.base_text && n.index_text;
}

bool isNegativeLiteral(const std::string& s) { return s.size() > 1 && s[0] == '-' && std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isdigit(c); }); }

// ---- out of bounds ---------------------------------------------------------------------------------------------------

class OutOfBoundsRule : public Rule {
public:
    const char* id() const override { return "out_of_bounds"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        const Node& fn = c.function;

        // container -> the variable it was created with: vector<int> v(n);
        std::map<std::string, std::string> sized_by;
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            if (n.type != "VAR_DECL") return;
            for (const Node& ch : n.children) {
                std::function<const Node*(const Node&)> ctor = [&](const Node& x) -> const Node* {
                    if (x.type == "CALL_EXPR" && !x.spelling.empty() && x.children.size() >= 1 && x.spelling == "vector") return &x;
                    for (const Node& y : x.children) if (const Node* r = ctor(y)) return r;
                    return nullptr;
                };
                if (const Node* call = ctor(ch)) {
                    for (const Node& a : call->children) {
                        if (a.type == "TEMPLATE_REF" || a.type == "TYPE_REF") continue;
                        const Node& first = core(a);
                        if (first.type == "DECL_REF_EXPR") sized_by[n.spelling] = first.spelling;
                        break;
                    }
                }
            }
        });

        std::set<int> reported;
        auto report = [&](int line, std::string msg) {
            if (reported.insert(line).second) out.push_back(makeFinding(line, "high", "out_of_bounds", std::move(msg)));
        };

        // for (...; i <= a.size(); ...) { ... a[i] ... }    and    for (...; i <= n; ...) with a sized by n
        ast::dfsPreorder(fn, [&](const Node& loop, int) {
            if (loop.type != "FOR_STMT" || loop.children.size() < 2) return;
            const Node* cond = nullptr;
            for (size_t i = 0; i + 1 < loop.children.size(); ++i) {
                const Node& x = core(loop.children[i]);
                if (x.type == "BINARY_OPERATOR" && opOf(x) == "<=" && x.children.size() == 2) { cond = &x; break; }
            }
            if (!cond) return;
            const Node& var = core(cond->children[0]);
            const Node& bound = core(cond->children[1]);
            if (var.type != "DECL_REF_EXPR") return;
            std::string container, bound_var;
            if (bound.type == "CALL_EXPR" && (bound.spelling == "size" || bound.spelling == "length")) container = receiverName(bound);
            else if (bound.type == "DECL_REF_EXPR") bound_var = bound.spelling;
            else return;
            ast::dfsPreorder(loop.children.back(), [&](const Node& n, int) {
                if (!isSubscript(n) || compact(*n.index_text) != var.spelling) return;
                const std::string base = compact(*n.base_text);
                const bool by_size = !container.empty() && base == container;
                const auto it = sized_by.find(base);
                const bool by_var = !bound_var.empty() && it != sized_by.end() && it->second == bound_var;
                if (by_size)
                    report(n.line, "The loop runs while " + var.spelling + " <= " + container + ".size(), so " + base + "[" + var.spelling +
                                       "] reaches one element past the end. Use <.");
                else if (by_var)
                    report(n.line, "The loop runs while " + var.spelling + " <= " + bound_var + ", but " + base + " has only " + bound_var + " elements (valid indexes 0.." + bound_var +
                                       " - 1), so " + base + "[" + var.spelling + "] goes one past the end. Use <.");
            });
        });

        // a[a.size()], a[-1], a[n] with a sized n
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            if (!isSubscript(n)) return;
            const std::string base = compact(*n.base_text), index = compact(*n.index_text);
            if (index == base + ".size()" || index == base + ".length()")
                report(n.line, base + "[" + index + "] is one past the last element; the last valid index is " + index + " - 1.");
            else if (isNegativeLiteral(index))
                report(n.line, base + "[" + index + "] uses a negative index.");
            else if (const auto it = sized_by.find(base); it != sized_by.end() && it->second == index)
                report(n.line, base + " was created with " + index + " elements, so " + base + "[" + index + "] is one past the end.");
        });
    }
};

// ---- uninitialized variables -----------------------------------------------------------------------------------------

class UninitializedRule : public Rule {
public:
    const char* id() const override { return "uninitialized_variable"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        State st{out};
        const Node* body = ast::bodyOf(c.function);
        if (body) walk(*body, st);
    }

private:
    struct State {
        explicit State(std::vector<Finding>& o) : out(o) {}
        std::vector<Finding>& out;
        std::set<std::string> tracked, assigned, reported;
        std::vector<const Node*> loops;            // loops around the node being walked
    };

    static bool assignsInside(const Node& n, const std::string& v) { return mayModify(n, {v}); }

    static void read(const std::string& v, int line, State& st) {
        if (!st.tracked.count(v) || st.assigned.count(v) || st.reported.count(v)) return;
        for (const Node* loop : st.loops)                      // `last` is read at the top of a loop and written at the bottom: fine
            if (assignsInside(*loop, v)) return;
        st.reported.insert(v);
        st.out.push_back(makeFinding(line, "high", "uninitialized_variable", "'" + v + "' is read before it is given a value."));
    }

    static void walk(const Node& n, State& st) {
        const std::string& op = opOf(n);
        const auto isTracked = [&](const Node& x) {
            const Node& u = core(x);
            return u.type == "DECL_REF_EXPR" && st.tracked.count(u.spelling) > 0;
        };

        if (n.type == "UNARY_EXPR" || n.type == "CXX_UNARY_EXPR") return;      // sizeof(x) does not read x

        if (n.type == "VAR_DECL") {
            for (const Node& ch : n.children) walk(ch, st);
            st.tracked.erase(n.spelling);
            st.assigned.erase(n.spelling);
            st.reported.erase(n.spelling);
            if (n.children.empty() && n.dtype && isScalarType(*n.dtype)) st.tracked.insert(n.spelling);
            return;
        }
        if (n.type == "BINARY_OPERATOR" && op == "=" && n.children.size() == 2) {
            walk(n.children[1], st);
            if (isTracked(n.children[0])) st.assigned.insert(core(n.children[0]).spelling);
            else walk(n.children[0], st);
            return;
        }
        if (n.type == "COMPOUND_ASSIGNMENT_OPERATOR" && n.children.size() == 2) {
            walk(n.children[1], st);
            walk(n.children[0], st);                           // reads the variable first
            return;
        }
        if (n.type == "UNARY_OPERATOR" && op == "&" && !n.children.empty() && isTracked(n.children[0])) {
            st.assigned.insert(core(n.children[0]).spelling);  // its address is taken: something may fill it
            return;
        }
        if (n.type == "CALL_EXPR") {
            for (const Node& ch : n.children) {
                if (isTracked(ch)) st.assigned.insert(core(ch).spelling);          // may be an output parameter, or cin >> x
                else walk(ch, st);
            }
            return;
        }
        if (n.type == "DECL_REF_EXPR") { read(n.spelling, n.line, st); return; }

        if (n.type == "CXX_FOR_RANGE_STMT") {                  // [var, range, body]: the loop variable is filled by the loop
            st.loops.push_back(&n);
            for (size_t i = 1; i < n.children.size(); ++i) walk(n.children[i], st);
            st.loops.pop_back();
            return;
        }
        if (ast::isLoop(n.type)) {
            st.loops.push_back(&n);
            if (n.type == "FOR_STMT" && n.children.size() == 4) {                  // init, cond, body, then the increment
                walk(n.children[0], st);
                walk(n.children[1], st);
                walk(n.children[3], st);
                walk(n.children[2], st);
            } else {
                for (const Node& ch : n.children) walk(ch, st);
            }
            st.loops.pop_back();
            return;
        }
        for (const Node& ch : n.children) walk(ch, st);
    }
};

// ---- memory leaks ----------------------------------------------------------------------------------------------------

class MemoryLeakRule : public Rule {
public:
    const char* id() const override { return "memory_leak"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        const Node& fn = c.function;
        std::vector<const Node*> pointers;
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            if (n.type == "VAR_DECL" && n.dtype && !n.dtype->empty() && n.dtype->back() == '*' && !n.children.empty() &&
                skipImplicit(n.children.back()).type == "CXX_NEW_EXPR")
                pointers.push_back(&n);
        });
        for (const Node* p : pointers) {
            bool released = false;
            ast::dfsPreorder(fn, [&](const Node& n, int) {
                if (released) return;
                auto refsP = [&](const Node& x) { return complexity::referencedNames(x).count(p->spelling) > 0; };
                if (n.type == "CXX_DELETE_EXPR" && refsP(n)) released = true;
                else if (n.type == "RETURN_STMT" && !n.children.empty() && core(n.children[0]).type == "DECL_REF_EXPR" &&
                         core(n.children[0]).spelling == p->spelling) released = true;        // `return p;`: ownership goes to the caller
                else if (n.type == "CALL_EXPR") {
                    if (n.spelling == "free" && refsP(n)) released = true;
                    for (const Node& a : n.children) {                                          // handed to something else
                        const Node& u = core(a);
                        if (u.type == "DECL_REF_EXPR" && u.spelling == p->spelling) released = true;
                    }
                } else if (n.type == "BINARY_OPERATOR" && opOf(n) == "=" && n.children.size() == 2 && core(n.children[1]).type == "DECL_REF_EXPR" &&
                           core(n.children[1]).spelling == p->spelling) released = true;       // stored somewhere: q = p, node->next = p
                else if (n.type == "VAR_DECL" && n.spelling != p->spelling && !n.children.empty() && core(n.children.back()).type == "DECL_REF_EXPR" &&
                         core(n.children.back()).spelling == p->spelling) released = true;      // int* q = p;
            });
            if (!released)
                out.push_back(makeFinding(p->line, "medium", "memory_leak",
                                          "Memory allocated with new and stored in '" + p->spelling + "' is never deleted (use delete" +
                                              (skipImplicit(p->children.back()).children.empty() ? "" : "[] for an array") + ", or a std::vector / smart pointer)."));
        }
    }
};

} // namespace

void addMemoryRules(RuleList& rules) {
    rules.push_back(std::make_unique<OutOfBoundsRule>());
    rules.push_back(std::make_unique<UninitializedRule>());
    rules.push_back(std::make_unique<MemoryLeakRule>());
}

} // namespace nc::checks
