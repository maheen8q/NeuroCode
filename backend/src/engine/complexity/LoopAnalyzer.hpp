#pragma once
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/ast/Node.hpp"
#include "engine/complexity/RecurrenceSolver.hpp"

namespace nc::complexity {

// How many times a loop runs, as a function of the input size n (see Order: one variable).
enum class TripKind {
    Constant,       // bounded by a literal or a constant: for (j = 0; j < 26; ++j)
    Logarithmic,    // the variable is multiplied or divided each round, or the search interval is halved
    Sqrt,           // i * i <= n, i <= sqrt(n)
    Linear,         // steps by a constant up to a bound that grows with the input, or drains a container
    Unknown,        // the analyzer cannot follow the loop variable
    Infinite,       // while (true) with no break and no return
};

struct LoopInfo {
    int line = 0;
    std::string type;               // FOR_STMT, WHILE_STMT, DO_STMT, CXX_FOR_RANGE_STMT
    int depth = 1;                  // 1 = outermost loop of the function
    int parent = -1;                // index of the enclosing loop in FunctionLoops::loops, -1 if none
    TripKind trip = TripKind::Unknown;
    Order trip_order = Order::unknownOrder();
    std::string variable;           // the loop variable ("i", "lo / hi"), "" if there is none
    std::string reason;             // one phrase for the derivation: why this trip count
    bool early_exit = false;        // a break or return inside: the trip count is an upper bound
    bool assumed = false;           // the trip count rests on an assumption (a container drain, ...)
    Order body_cost;                // cost of one iteration
    Order cost;                     // trip_order * body_cost
};

// One call from a function to itself, as the recurrence solver needs it.
struct RecursiveCall {
    enum class Shrink { Divide, Subtract, Unknown };
    int line = 0;
    Shrink shrink = Shrink::Unknown;
    double factor = 1.0;            // Divide: the divisor, Subtract: the amount
    bool factor_assumed = false;    // the literal's value is not in the AST, so 2 (Divide) or 1 (Subtract) is assumed
    int loop_depth = 0;             // loops enclosing the call: > 0 means it runs once per iteration
    Order loop_factor;              // product of the trip counts of those loops (O(1) when none)
    std::string argument;           // the argument that shrinks ("n - 1", "mid", "(lo + hi) / 2"): for the derivation
    std::string pivot;              // Unknown shrink only: the local `p` in an argument `p - c` or `p + c` (quicksort splits)
};

struct FunctionLoops {
    std::string function;
    int line = 0;
    std::vector<LoopInfo> loops;                 // preorder, source order
    int max_depth = 0;
    Order work;                                  // cost of one call without its recursive calls: loops, then calls
    std::vector<RecursiveCall> recursive_calls;  // for IF / ?: only the branch with more calls (one branch runs)
    bool maybe_memoized = false;                 // the function caches results: the recursion tree is smaller than it looks
    std::vector<std::string> derivation;         // human readable lines about the loops
};

// Called for every CALL_EXPR that is not a recursive call. Return the cost of that call, or nullopt for O(1).
// TimeComplexity passes the library cost table (std::sort, std::find, ...) and user-function summaries here.
using CallCostFn = std::function<std::optional<Order>(const ast::Node& call)>;

// Called once for every node of the function body, in cost order. loop_depth = loops around the node, loop_factor =
// product of their trip counts. SpaceComplexity uses it to find allocations and what grows them.
using NodeVisitFn = std::function<void(const ast::Node& node, int loop_depth, const Order& loop_factor)>;

struct AnalyzerOptions {
    CallCostFn call_cost;                        // empty: every call is O(1)
    NodeVisitFn on_node;                         // empty: not called
    std::vector<std::string> constants;          // names of variables known to be small constants (global const int N = 100)
};

// Analyzes one function-like node (FUNCTION_DECL, CXX_METHOD, ...). Never throws; a function without a body
// yields no loops and work = O(1).
FunctionLoops analyzeFunction(const ast::Node& function, const AnalyzerOptions& options = {});

// Every function found under root. Fills options.constants from `const` variables initialised with a literal
// anywhere in the translation unit.
std::vector<FunctionLoops> analyzeAll(const ast::Node& root, AnalyzerOptions options = {});

const char* toString(TripKind k);

// Expression helpers shared with TimeComplexity and SpaceComplexity.
const ast::Node& unwrapExpr(const ast::Node& n);                    // strips UNEXPOSED_EXPR, PAREN_EXPR and casts
bool isConstantExpression(const ast::Node& n, const std::set<std::string>& constants = {});   // literals and names of constants
std::set<std::string> referencedNames(const ast::Node& n);          // spellings of every DECL_REF_EXPR below n

} // namespace nc::complexity
