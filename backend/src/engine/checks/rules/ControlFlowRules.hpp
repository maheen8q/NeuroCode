#pragma once
#include "engine/checks/rules/Rule.hpp"

namespace nc::checks {

// Rules about how control moves through a function. Built on the CFG: Reachability and Dominators.
//   unreachable         code that follows return, break, continue or an endless loop            (medium)
//   missing_return      a non-void function whose end can be reached without a return           (high)
//   infinite_loop       a loop with no exit, or whose condition nothing in the loop changes     (high / medium)
//   infinite_recursion  a function that calls itself on every path, or with its own arguments   (high / medium)
void addControlFlowRules(RuleList& rules);

} // namespace nc::checks
