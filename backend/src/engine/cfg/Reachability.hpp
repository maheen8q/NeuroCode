#pragma once
#include <vector>

#include "engine/cfg/Cfg.hpp"

namespace nc::cfg {

// Reachability questions on one function's CFG. Every function returns an empty result for a graph that was
// not built (cfg.unsupported non-empty, no blocks), so callers need no special case.

// result[i] is true when some path from the entry reaches block i.
std::vector<bool> reachableBlocks(const Cfg& cfg);

// result[i] is true when block i can reach the exit block (reverse reachability).
std::vector<bool> canReachExit(const Cfg& cfg);

// Blocks that carry source lines but that no path from the entry reaches: code after return/break/continue,
// the rest of a body after an infinite loop. Ids are ascending.
std::vector<int> unreachableBlocks(const Cfg& cfg);

// Source lines of those blocks, ascending and without duplicates (what the UI highlights as dead code).
std::vector<int> unreachableLines(const Cfg& cfg);

// Reachable blocks from which the exit can never be reached: execution that enters them runs forever
// (`while (true)` with no break or return). Ids are ascending.
std::vector<int> blocksThatCannotReachExit(const Cfg& cfg);

// True when the exit is reachable at all from the entry.
bool exitReachable(const Cfg& cfg);

// Every reachable path ends in a `return` block: execution never falls off the end of the body. For a
// non-void function false means a missing return. Paths that never end (infinite loops) do not count against it.
bool allPathsReturn(const Cfg& cfg);

} // namespace nc::cfg
