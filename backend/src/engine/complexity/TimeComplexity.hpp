#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/ast/Node.hpp"
#include "engine/complexity/LoopAnalyzer.hpp"
#include "engine/complexity/RecurrenceSolver.hpp"

namespace nc::complexity {

// ---- the program: functions and who calls whom (shared with SpaceComplexity) -------------------------------------

struct ProgramIndex {
    std::vector<const ast::Node*> functions;                      // with a body, source order
    std::map<std::string, const ast::Node*> by_name;              // overloads: the first definition wins
    std::map<std::string, std::set<std::string>> callees;         // user functions called, self calls left out
    std::vector<std::string> roots;                               // called by no other function, source order
    std::set<std::string> constants;                              // const variables initialised with a literal
};

ProgramIndex buildIndex(const ast::Node& root);

// The function the complexity is asked about. `hint` wins when it names a function. Otherwise the last root that is
// not `main` (helpers come first in a solution file, the entry point last), else main, else the last function.
// Returns "" when there is no function.
std::string chooseEntry(const ProgramIndex& index, const std::string& hint = "");

// ---- cost of library calls ----------------------------------------------------------------------------------------

// Cost of one CALL_EXPR that is not a user function: the algorithm and container tables of the standard library
// (std::sort n log n, std::find n, lower_bound log n, unordered_set::insert 1, map::operator[] log n,
// vector::insert n, constructing a container from n elements n, ...). nullopt means O(1) or not in the table.
// Container operations are told apart by the receiver's declared type.
std::optional<Order> libraryCallCost(const ast::Node& call, const std::set<std::string>& constants = {});

// How many elements a container constructor call creates: vector<int>(n) is n, vector<vector<int>>(n, vector<int>(m))
// is n * n (one size variable), vector<int>(5) and an initializer list are constant, a copy of a container is n.
Order constructionSize(const ast::Node& ctor_call, const std::set<std::string>& constants = {});

// True for names of standard containers and string (the spelling of their constructor calls).
bool isContainerName(const std::string& name);

// The container family of a declared type, from the text before the first '<': "vector<int> &" -> "vector".
std::string containerFamily(const std::string& dtype);     // "" when it is not a standard container or string

// ---- the analysis --------------------------------------------------------------------------------------------------

struct FunctionTime {
    std::string name;
    int line = 0;
    Order cost;                          // worst-case time of one call, in n
    bool recursive = false;
    std::string method;                  // "loops", "master theorem", "unrolling", "memoised states", "unsolved", ...
    std::vector<std::string> derivation;
};

struct TimeAnalysis {
    std::string entry;                           // the function the answer is about ("" if the code has none)
    Order time = Order::unknownOrder();
    std::vector<std::string> derivation;         // lines for the UI, the entry's own steps first
    std::vector<FunctionTime> functions;         // source order
    std::vector<std::string> assumptions;        // distinct, in the order they were made
    bool confident = false;                      // false when the result is unknown or rests on an assumption
};

struct TimeOptions {
    std::string entry;                           // optional hint for chooseEntry
};

// Worst-case running time of the entry function, with callees resolved bottom-up:
//  - loops: product of trip counts (LoopAnalyzer);
//  - library calls: the table above, added to the loop that contains them;
//  - user function calls: that function's own cost (arguments are not tracked: a callee is charged as if it
//    received the full input);
//  - direct recursion: RecurrenceSolver; a cache guard (`if (memo[n] != -1) return memo[n];`) turns the call tree
//    into n^k states times the work per state; quicksort splits are charged their worst case, n * work;
//  - mutual recursion, recursion inside a loop and unrecognised shrinking give an unknown order.
TimeAnalysis analyzeTime(const ast::Node& root, const TimeOptions& options = {});

} // namespace nc::complexity
