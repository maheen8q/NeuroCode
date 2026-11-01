#include "engine/checks/rules/SuspiciousCodeRules.hpp"

#include "engine/ast/Traversal.hpp"
#include "engine/checks/rules/Rule.hpp"

namespace nc::checks {

using ast::Node;

namespace {

class AssignmentInConditionRule : public Rule {
public:
    const char* id() const override { return "assignment_in_condition"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            const Node* cond = conditionOf(n);
            if (!cond) return;
            const Node& e = skipImplicit(*cond);                 // a PAREN_EXPR here means the author wrote ((x = f())) on purpose
            if (e.type != "BINARY_OPERATOR" || !e.op || *e.op != "=" || e.children.size() != 2) return;
            out.push_back(makeFinding(e.line > 0 ? e.line : n.line, "medium", "assignment_in_condition",
                                      "The condition assigns with '=' instead of comparing with '=='. If the assignment is intended, wrap it in a second pair of parentheses."));
        });
    }
};

class EmptyLoopBodyRule : public Rule {
public:
    const char* id() const override { return "empty_loop_body"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            if (n.type != "FOR_STMT" && n.type != "WHILE_STMT") return;
            const Node* body = bodyOfLoop(n);
            if (!body || body->type != "NULL_STMT") return;
            out.push_back(makeFinding(n.line, n.type == "WHILE_STMT" ? "medium" : "low", "empty_loop_body",
                                      "The ';' right after the loop header is the whole loop body, so the loop does nothing but repeat. If that is intended write {} to say so; otherwise remove the ';'."));
        });
    }
};

class FloatEqualityRule : public Rule {
public:
    const char* id() const override { return "float_equality"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            if (n.type != "BINARY_OPERATOR" || !n.op || (*n.op != "==" && *n.op != "!=") || n.children.size() != 2) return;
            const Node& l = core(n.children[0]);
            const Node& r = core(n.children[1]);
            if (!isFloating(l) && !isFloating(r)) return;
            // x != x is the classic NaN test: both sides are the same variable
            if (l.type == "DECL_REF_EXPR" && r.type == "DECL_REF_EXPR" && l.spelling == r.spelling) return;
            out.push_back(makeFinding(n.line, "medium", "float_equality",
                                      "Comparing floating-point values with '" + *n.op + "' is unreliable because of rounding. Compare against a small epsilon instead, e.g. fabs(a - b) < 1e-9."));
        });
    }

private:
    static bool isFloating(const Node& e) {
        if (!e.dtype) return false;
        std::string t;
        for (char ch : *e.dtype) if (ch != ' ' && ch != '&') t += ch;
        if (t.rfind("const", 0) == 0) t = t.substr(5);
        return t == "float" || t == "double" || t == "longdouble";
    }
};

class UnusedVariableRule : public Rule {
public:
    const char* id() const override { return "unused_variable"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        std::set<std::string> used;
        ast::dfsPreorder(c.function, [&](const Node& n, int) { if (n.type == "DECL_REF_EXPR") used.insert(n.spelling); });
        std::set<std::string> reported;
        ast::dfsPreorder(c.function, [&](const Node& n, int) {
            if (n.type != "VAR_DECL" || n.spelling.empty() || n.spelling[0] == '_' || used.count(n.spelling) || !n.dtype) return;
            const std::string& t = *n.dtype;
            if (!(isScalarType(t) || !containerKind(t).empty())) return;                  // classes may do work in their constructor
            if (!reported.insert(n.spelling).second) return;
            out.push_back(makeFinding(n.line, "low", "unused_variable", "'" + n.spelling + "' is declared but never used."));
        });
    }

private:
    static std::string containerKind(const std::string& t) {
        for (const char* k : {"vector<", "string", "map<", "set<", "unordered_map<", "unordered_set<", "deque<", "array<"})
            if (t.find(k) != std::string::npos) return k;
        return {};
    }
};

} // namespace

void addSuspiciousCodeRules(RuleList& rules) {
    rules.push_back(std::make_unique<AssignmentInConditionRule>());
    rules.push_back(std::make_unique<EmptyLoopBodyRule>());
    rules.push_back(std::make_unique<FloatEqualityRule>());
    rules.push_back(std::make_unique<UnusedVariableRule>());
}

} // namespace nc::checks
