#pragma once
#include "engine/checks/rules/Rule.hpp"

namespace nc::checks {

// Rules about memory.
//   out_of_bounds           for (i = 0; i <= a.size(); ...) a[i]; a[a.size()]; a[-1]; a[n] on an array sized n   (high)
//   uninitialized_variable  a local scalar read before anything gives it a value                               (high)
//   memory_leak             `p = new ...` where p is never deleted, returned or handed to anything             (medium)
void addMemoryRules(RuleList& rules);

} // namespace nc::checks
