#pragma once
#include <vector>

#include "engine/cfg/Cfg.hpp"

namespace nc::cfg {

// Immediate dominators by Cooper, Harvey and Kennedy ("A Simple, Fast Dominance Algorithm"). idom[entry] == entry
// and idom[b] == -1 for a block no path from the entry reaches. Empty for a graph that was not built.
std::vector<int> immediateDominators(const Cfg& cfg);

// True when every path from the entry to b passes through a (a block dominates itself). False if either is
// unreachable or out of range.
bool dominates(const std::vector<int>& idom, int a, int b);

// Children of each block in the dominator tree, ascending. children[b] is empty for leaves and unreachable blocks.
std::vector<std::vector<int>> dominatorTree(const std::vector<int>& idom);

struct NaturalLoop {
    int header = 0;
    std::vector<int> latches;      // sources of the back edges into the header, ascending
    std::vector<int> blocks;       // every block of the loop, header included, ascending
    int parent = -1;               // index in the vector naturalLoops() returned of the innermost enclosing loop, or -1
    int depth = 1;                 // 1 for an outermost loop
};

// A back edge is u -> v where v dominates u. This is the structural definition: it does not trust the builder's
// "back" edge label. Back edges into one header form one loop. Ordered by header id.
std::vector<NaturalLoop> naturalLoops(const Cfg& cfg);

// True when no edge leaves the loop: once entered it never terminates (`while (true)` without break or return).
bool loopHasNoExit(const Cfg& cfg, const NaturalLoop& loop);

// Deepest loop nesting in the graph (0 when there is no loop). Matches ast::maxLoopDepth for structured code.
int maxLoopDepth(const std::vector<NaturalLoop>& loops);

// True when every retreating edge of a depth-first search is a back edge, i.e. loops have single entries. Code
// without goto is always reducible; the check guards against a builder bug and keeps the loop analysis honest.
bool isReducible(const Cfg& cfg);

} // namespace nc::cfg
