#include "engine/checks/rules/ControlFlowRules.hpp"

#include <algorithm>
#include <map>

#include "engine/ast/Traversal.hpp"
#include "engine/cfg/Dominators.hpp"
#include "engine/cfg/Reachability.hpp"

namespace nc::checks {

using ast::Node;

namespace {

std::string pluralLines(int a, int b) { return a == b ? "Line " + std::to_string(a) : "Lines " + std::to_string(a) + "-" + std::to_string(b); }

// ---- unreachable -------------------------------------------------------------------------------------------------------

class UnreachableRule : public Rule {
public:
    const char* id() const override { return "unreachable"; }
    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        if (!c.cfg) return;
        const std::vector<int> lines = cfg::unreachableLines(*c.cfg);
        for (size_t i = 0; i < lines.size();) {
            size_t j = i;
            while (j + 1 < lines.size() && lines[j + 1] - lines[j] <= 2) ++j;       // a blank line between two dead statements
            out.push_back(makeFinding(lines[i], "medium", "unreachable",
                                      pluralLines(lines[i], lines[j]) + " can never run: it follows a return, break, continue or a loop that never ends."));
            i = j + 1;
        }
    }
};

// ---- missing return ----------------------------------------------------------------------------------------------------

bool endsWithNoReturnCall(const Node& body) {
    if (body.children.empty()) return false;
    const Node& last = complexity::unwrapExpr(body.children.back());
    static const std::set<std::string> noreturn = {"exit", "abort", "_Exit", "quick_exit", "terminate", "__builtin_unreachable"};
    return last.type == "CALL_EXPR" && noreturn.count(last.spelling) > 0;
}

class MissingReturnRule : public Rule {
public:
    const char* id() const override { return "missing_return"; }
    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        if (!c.cfg || !c.cfg->falls_off_end) return;
        const Node& fn = c.function;
        if (fn.spelling == "main" || (fn.type != "FUNCTION_DECL" && fn.type != "CXX_METHOD")) return;
        const std::string& rt = c.return_type;
        if (rt.empty() || rt == "void" || rt.rfind("auto", 0) == 0 || rt.rfind("decltype", 0) == 0) return;
        const Node* body = ast::bodyOf(fn);
        if (!body || endsWithNoReturnCall(*body)) return;
        const int line = !body->children.empty() && body->children.back().line > 0 ? body->children.back().line : fn.line;
        out.push_back(makeFinding(line, "high", "missing_return",
                                  "'" + fn.spelling + "' returns " + rt + ", but control can reach the end of the function without a return statement."));
    }
};

// ---- infinite loops ----------------------------------------------------------------------------------------------------

const Node* forCondition(const Node& loop) {
    for (size_t i = 0; i + 1 < loop.children.size(); ++i) {
        const Node& c = core(loop.children[i]);
        const std::string& op = c.op ? *c.op : std::string();
        if (c.type == "BINARY_OPERATOR" && (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "!=")) return &loop.children[i];
    }
    return nullptr;
}

class InfiniteLoopRule : public Rule {
public:
    const char* id() const override { return "infinite_loop"; }
    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        // 1. A natural loop with no edge leaving it.
        if (c.cfg) {
            for (const cfg::NaturalLoop& loop : cfg::naturalLoops(*c.cfg)) {
                if (!cfg::loopHasNoExit(*c.cfg, loop)) continue;
                const auto& lines = c.cfg->blocks[static_cast<size_t>(loop.header)].lines;
                const int line = lines.empty() ? c.function.line : lines.front();
                out.push_back(makeFinding(line, "high", "infinite_loop",
                                          "This loop never ends: nothing inside it can break, return, or make the loop condition false."));
            }
        }
        // 2. A condition on local variables that the loop never changes.
        std::map<std::string, std::string> types;                  // locals and parameters with their declared types
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            if ((n.type == "VAR_DECL" || n.type == "PARM_DECL") && n.dtype) types[n.spelling] = *n.dtype;
        });
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            if (n.type != "WHILE_STMT" && n.type != "FOR_STMT" && n.type != "DO_STMT") return;
            const Node* cond = n.type == "FOR_STMT" ? forCondition(n) : conditionOf(n);
            if (!cond || complexity::referencedNames(*cond).empty()) return;
            if (containsCall(*cond)) return;
            std::set<std::string> vars = complexity::referencedNames(*cond);
            for (const std::string& v : vars) {
                const auto it = types.find(v);
                if (it == types.end() || !isScalarType(it->second)) return;       // globals, containers, iterators: not judged
            }
            if (mayModify(n, vars) || hasExit(n)) return;
            std::string list;
            for (const std::string& v : vars) list += (list.empty() ? "" : ", ") + v;
            out.push_back(makeFinding(n.line, "medium", "infinite_loop",
                                      "The loop condition depends on " + list + ", but nothing in the loop changes " + (vars.size() == 1 ? "it" : "them") +
                                          " and there is no break or return: once entered, the loop never ends."));
        });
    }

private:
    static bool containsCall(const Node& n) {
        if (n.type == "CALL_EXPR") return true;
        for (const Node& c : n.children) if (containsCall(c)) return true;
        return false;
    }
};

// ---- infinite recursion ------------------------------------------------------------------------------------------------

// Like mayModify, but the arguments of the function's own recursive calls do not count as a change: passing a parameter
// on to itself changes nothing.
bool changesOutsideSelfCalls(const Node& n, const std::set<std::string>& names, const std::string& self) {
    if (n.type == "CALL_EXPR" && n.spelling == self) {
        for (const Node& c : n.children)
            if (core(c).type != "DECL_REF_EXPR" && mayModify(c, names)) return true;           // f(n++) changes n
        return false;
    }
    const std::string& op = n.op ? *n.op : std::string();
    auto isName = [&](const Node& x) { const Node& u = core(x); return u.type == "DECL_REF_EXPR" && names.count(u.spelling) > 0; };
    if (((n.type == "BINARY_OPERATOR" && op == "=") || n.type == "COMPOUND_ASSIGNMENT_OPERATOR") && !n.children.empty() && isName(n.children[0])) return true;
    if (n.type == "UNARY_OPERATOR" && (op == "++" || op == "--" || op == "&") && !n.children.empty() && isName(n.children[0])) return true;
    if (n.type == "CALL_EXPR")
        for (const Node& c : n.children) if (isName(c)) return true;                           // may be passed by reference
    for (const Node& c : n.children) if (changesOutsideSelfCalls(c, names, self)) return true;
    return false;
}

class InfiniteRecursionRule : public Rule {
public:
    const char* id() const override { return "infinite_recursion"; }
    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        if (c.loops.recursive_calls.empty()) return;
        const Node& fn = c.function;
        const Node* body = ast::bodyOf(fn);
        if (!body) return;

        // No branch, loop or short-circuit anywhere: the first call to itself is reached every time.
        bool branches = false;
        ast::dfsPreorder(*body, [&](const Node& n, int) {
            const std::string& op = n.op ? *n.op : std::string();
            if (n.type == "IF_STMT" || n.type == "CONDITIONAL_OPERATOR" || n.type == "SWITCH_STMT" || ast::isLoop(n.type) ||
                n.type == "LAMBDA_EXPR" || (n.type == "BINARY_OPERATOR" && (op == "&&" || op == "||")))
                branches = true;
        });
        if (!branches) {
            out.push_back(makeFinding(c.loops.recursive_calls.front().line, "high", "infinite_recursion",
                                      "'" + fn.spelling + "' calls itself on every path and has no base case, so the recursion never stops."));
            return;
        }

        // A call that passes along exactly the parameters it was given, when nothing changes them.
        std::vector<std::string> params;
        for (const Node& p : fn.children) if (p.type == "PARM_DECL") params.push_back(p.spelling);
        if (params.empty()) return;
        const std::set<std::string> names(params.begin(), params.end());
        if (changesOutsideSelfCalls(*body, names, fn.spelling)) return;       // includes passing a parameter to another call
        ast::dfsPreorder(*body, [&](const Node& n, int) {
            if (n.type != "CALL_EXPR" || n.spelling != fn.spelling || n.children.size() != params.size() + 1) return;
            for (size_t i = 0; i < params.size(); ++i) {
                const Node& a = core(n.children[i + 1]);
                if (a.type != "DECL_REF_EXPR" || a.spelling != params[i]) return;
            }
            out.push_back(makeFinding(n.line, "medium", "infinite_recursion",
                                      "'" + fn.spelling + "' calls itself with the same arguments it received and nothing changes them, so it makes no progress."));
        });
    }
};

} // namespace

void addControlFlowRules(RuleList& rules) {
    rules.push_back(std::make_unique<UnreachableRule>());
    rules.push_back(std::make_unique<MissingReturnRule>());
    rules.push_back(std::make_unique<InfiniteLoopRule>());
    rules.push_back(std::make_unique<InfiniteRecursionRule>());
}

} // namespace nc::checks
