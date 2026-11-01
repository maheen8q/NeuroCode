#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/AnalysisResult.hpp"
#include "engine/ast/Node.hpp"
#include "engine/cfg/Cfg.hpp"
#include "engine/complexity/LoopAnalyzer.hpp"

namespace nc::checks {

// What a rule sees: one function, its control flow graph, and what the loop analyzer already knows about it.
struct CheckContext {
    const ast::Node& function;                      // FUNCTION_DECL or CXX_METHOD with a body
    const cfg::Cfg* cfg = nullptr;                  // null when no graph was built (goto, try/catch)
    const complexity::FunctionLoops& loops;         // loops, recursive calls
    const std::set<std::string>& constants;         // global and local const variables initialised with a literal
    std::string return_type;                        // "int", "void", "vector<int>"; "" when unknown
};

// One check. The id is also the `type` of every finding the rule reports, so a rule can be switched off by name.
class Rule {
public:
    virtual ~Rule() = default;
    virtual const char* id() const = 0;
    virtual void check(const CheckContext& ctx, std::vector<Finding>& out) const = 0;
};

using RuleList = std::vector<std::unique_ptr<Rule>>;

// ---- helpers shared by the rules -----------------------------------------------------------------------------------

Finding makeFinding(int line, const char* severity, const char* type, std::string message);

// UNEXPOSED_EXPR is libclang's wrapper for implicit conversions: it hides nothing the user wrote.
const ast::Node& skipImplicit(const ast::Node& n);
// Also strips PAREN_EXPR and casts. Use skipImplicit when the user's own parentheses matter (`if ((x = f()))`).
const ast::Node& core(const ast::Node& n);

std::string compact(const std::string& s);                  // "a . size ( )" -> "a.size()"
std::string returnTypeOf(const ast::Node& function);        // "int (vector<int> &, int)" -> "int"
bool isScalarType(const std::string& dtype);                // int, long long, double, bool, char, unsigned, T *
bool isNarrowIntType(const std::string& dtype);             // 32 bit or smaller: int, unsigned int, short, char
bool isWideIntType(const std::string& dtype);               // 64 bit: long, long long, unsigned long long, int64_t, size_t

// Short source-like text for messages: "a[i]", "n * n", "lo + hi"; "..." for anything complicated.
std::string exprText(const ast::Node& n);

// Name of the variable at the root of a member or operator call's receiver: `adj[u].push_back(v)` -> "adj".
std::string receiverName(const ast::Node& call);

// The condition of an if, while or do-while; nullptr when the shape is not the usual one.
const ast::Node* conditionOf(const ast::Node& stmt);
// The statement a loop repeats: the last child.
const ast::Node* bodyOfLoop(const ast::Node& loop);

// True when `n` writes to one of `names`: =, op=, ++, --, &name, or passes it to a call (which may take a reference).
bool mayModify(const ast::Node& n, const std::set<std::string>& names);
// break, return, goto or throw somewhere in the subtree (a break inside a nested loop or switch does not count).
bool hasExit(const ast::Node& n);

} // namespace nc::checks
