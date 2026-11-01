#pragma once
#include <string>
#include <vector>

#include "engine/AnalysisResult.hpp"
#include "engine/complexity/TimeComplexity.hpp"

namespace nc::complexity {

// One block of memory the function creates: a container, an array, a `new`, a by-value copy of the input.
struct Allocation {
    std::string name;
    int line = 0;
    std::string kind;            // "vector", "string", "unordered_set", ..., "array", "new", "copy", "temporary", "global"
    Order elements;              // how many elements it ends up holding
    bool assumed = false;        // rests on an assumption (a large fixed-size array is sized for the input)
    std::string text;            // the derivation line
};

struct FunctionSpace {
    std::string name;
    int line = 0;
    std::vector<Allocation> allocations;
    Order locals;                // the largest of the allocations: simultaneous blocks of one order add up to that order
    Order callees;               // the largest callee, which is live while the call runs
    Order depth;                 // recursion depth, O(1) when not recursive
    bool recursive = false;
    Order total;                 // auxiliary space of one call to this function, recursion stack included
    std::vector<std::string> derivation;
};

struct SpaceAnalysis {
    std::string entry;
    Order space = Order::unknownOrder();
    std::vector<std::string> derivation;
    std::vector<FunctionSpace> functions;      // source order
    std::vector<std::string> assumptions;
    bool confident = false;
};

struct SpaceOptions {
    std::string entry;                         // optional hint for chooseEntry
};

// Auxiliary space of the entry function: memory the code allocates beyond the input it is given.
//  - containers, arrays and `new`: their element counts (a size argument, a growing loop, array dimensions); a
//    container that is only appended to counts the appends of its widest loop;
//  - an input passed by value is copied: that is O(n);
//  - the call stack: recursion depth times the memory of one frame, O(log n) for halving recursion and O(n) for
//    recursion that peels off one element, which is also assumed when the shrinking is not recognised;
//  - callees: the largest one, library calls that allocate (stable_sort, substr, ...) included;
//  - global arrays and containers.
// An array dimension of up to 256 is a constant (letters, ASCII codes); a larger one is assumed to be sized for the input
// and counts as n, so int dp[1005][1005] is O(n^2).
SpaceAnalysis analyzeSpace(const ast::Node& root, const SpaceOptions& options = {});

// The facade the engine calls: both analyses and one Complexity for the API. Unknown orders print as "unknown".
// derivation = the time lines, then the space lines, then one "Assumption: ..." line for each assumption made.
Complexity analyzeComplexity(const ast::Node& root, const std::string& entry = "");

} // namespace nc::complexity
