#include "engine/checks/rules/ArithmeticRules.hpp"

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

bool isConstant(const Node& n, const std::set<std::string>& constants) { return complexity::isConstantExpression(n, constants); }

// `a * b` that C++ evaluates in a type of 32 bits or fewer, reached through implicit conversions only.
const Node* narrowProduct(const Node& expr, const std::set<std::string>& constants) {
    const Node& e = skipImplicit(expr);
    if (e.type != "BINARY_OPERATOR" || opOf(e) != "*" || e.children.size() != 2 || !isNarrowIntType(e.dtype.value_or(""))) return nullptr;
    if (isConstant(e.children[0], constants) && isConstant(e.children[1], constants)) return nullptr;     // folded at compile time
    return &e;
}

class OverflowRiskRule : public Rule {
public:
    const char* id() const override { return "overflow_risk"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        const Node& fn = c.function;
        std::set<std::string> int_locals;                               // local `int` variables: candidates for accumulation overflow
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            if (n.type == "VAR_DECL" && n.dtype && *n.dtype == "int") int_locals.insert(n.spelling);
        });
        std::set<std::string> reported;                                 // one accumulator finding per variable

        auto widened = [&](const Node& n, const Node& rhs, const std::string& target_type) {
            if (!isWideIntType(target_type)) return;
            if (const Node* prod = narrowProduct(rhs, c.constants))
                out.push_back(makeFinding(n.line, "medium", "overflow_risk",
                                          exprText(*prod) + " is computed in " + prod->dtype.value_or("int") + " and only then converted to " + target_type +
                                              ", so it can overflow first. Convert an operand before multiplying: (" + target_type + ")" +
                                              exprText(prod->children[0]) + " * " + exprText(prod->children[1]) + "."));
        };

        ast::dfsPreorder(fn, [&](const Node& n, int loop_depth) {
            const std::string& op = opOf(n);

            // (lo + hi) / 2   and   (lo + hi) >> 1
            if (n.type == "BINARY_OPERATOR" && (op == "/" || op == ">>") && n.children.size() == 2 && core(n.children[1]).type == "INTEGER_LITERAL") {
                const Node& sum = core(n.children[0]);
                if (sum.type == "BINARY_OPERATOR" && opOf(sum) == "+" && sum.children.size() == 2 && isNarrowIntType(sum.dtype.value_or("")) &&
                    !isConstant(sum.children[0], c.constants) && !isConstant(sum.children[1], c.constants))
                    out.push_back(makeFinding(n.line, "medium", "overflow_risk",
                                              "(" + exprText(sum) + ") can overflow " + sum.dtype.value_or("int") + " when both values are large. Write " +
                                                  exprText(sum.children[0]) + " + (" + exprText(sum.children[1]) + " - " + exprText(sum.children[0]) +
                                                  ") / 2 instead."));
            }

            // long long x = a * b;   x = a * b;   x += a * b;   return a * b;
            if (n.type == "VAR_DECL" && n.dtype && !n.children.empty()) widened(n, n.children.back(), *n.dtype);
            if ((n.type == "BINARY_OPERATOR" && op == "=") || n.type == "COMPOUND_ASSIGNMENT_OPERATOR") {
                if (n.children.size() == 2) widened(n, n.children[1], core(n.children[0]).dtype.value_or(""));
            }
            if (n.type == "RETURN_STMT" && !n.children.empty()) widened(n, n.children[0], c.return_type);

            // int total += ...; int product *= ...; inside a loop
            if (n.type == "COMPOUND_ASSIGNMENT_OPERATOR" && loop_depth > 0 && n.children.size() == 2 && (op == "*=" || op == "+=")) {
                const Node& lhs = core(n.children[0]);
                if (lhs.type == "DECL_REF_EXPR" && int_locals.count(lhs.spelling) && !isConstant(n.children[1], c.constants) &&
                    reported.insert(lhs.spelling).second) {
                    if (op == "*=")
                        out.push_back(makeFinding(n.line, "medium", "overflow_risk",
                                                  "'" + lhs.spelling + " *= ...' multiplies an int inside a loop; the product can exceed the int range after a few iterations. Use long long (or reduce modulo the limit the problem gives)."));
                    else
                        out.push_back(makeFinding(n.line, "low", "overflow_risk",
                                                  "'" + lhs.spelling + "' is an int that sums values in a loop; with large inputs the total can exceed the int range. Use long long if the values or the length can be large."));
                }
            }
        });
    }
};

// ---- division by the size of a possibly empty container ---------------------------------------------------------------

struct SizeCall {
    std::string container;
    bool minus_one = false;
};

bool sizeCall(const Node& raw, SizeCall& out) {
    const Node& n = core(raw);
    if (n.type == "CALL_EXPR" && (n.spelling == "size" || n.spelling == "length") && !n.children.empty() && n.children[0].type == "MEMBER_REF_EXPR") {
        out.container = receiverName(n);
        out.minus_one = false;
        return !out.container.empty();
    }
    if (n.type == "BINARY_OPERATOR" && opOf(n) == "-" && n.children.size() == 2 && core(n.children[1]).type == "INTEGER_LITERAL") {
        SizeCall inner;
        if (sizeCall(n.children[0], inner)) { out = inner; out.minus_one = true; return true; }
    }
    return false;
}

class DivisionByZeroRule : public Rule {
public:
    const char* id() const override { return "division_by_zero"; }

    void check(const CheckContext& c, std::vector<Finding>& out) const override {
        const Node& fn = c.function;
        // Containers the function checks for emptiness: `x.empty()` anywhere, or x.size() in the condition of an if or ?:
        std::set<std::string> guarded;
        std::function<void(const Node&)> sizeInCondition = [&](const Node& n) {
            SizeCall s;
            if (sizeCall(n, s)) guarded.insert(s.container);
            for (const Node& ch : n.children) sizeInCondition(ch);
        };
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            if (n.type == "CALL_EXPR" && n.spelling == "empty" && !receiverName(n).empty()) guarded.insert(receiverName(n));
            if ((n.type == "IF_STMT" || n.type == "CONDITIONAL_OPERATOR") && !n.children.empty()) {
                const Node* cond = n.type == "IF_STMT" ? conditionOf(n) : &n.children[0];
                if (cond) sizeInCondition(*cond);
            }
        });
        ast::dfsPreorder(fn, [&](const Node& n, int) {
            const std::string& op = opOf(n);
            if (n.type != "BINARY_OPERATOR" || (op != "/" && op != "%") || n.children.size() != 2) return;
            SizeCall s;
            if (!sizeCall(n.children[1], s) || guarded.count(s.container)) return;
            out.push_back(makeFinding(n.line, "medium", "division_by_zero",
                                      std::string("Dividing by ") + s.container + ".size()" + (s.minus_one ? " - 1" : "") + " fails when " + s.container +
                                          (s.minus_one ? " has one element (or none)" : " is empty") + ". Check " + s.container + ".empty() first."));
        });
    }
};

} // namespace

void addArithmeticRules(RuleList& rules) {
    rules.push_back(std::make_unique<OverflowRiskRule>());
    rules.push_back(std::make_unique<DivisionByZeroRule>());
}

} // namespace nc::checks
