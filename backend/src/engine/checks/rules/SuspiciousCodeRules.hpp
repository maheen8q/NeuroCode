#pragma once
#include "engine/checks/rules/Rule.hpp"

namespace nc::checks {

// Rules about code that is probably not what the author meant.
//   assignment_in_condition  if (x = 5): '=' where '==' was meant; double parentheses mark it as intended   (medium)
//   empty_loop_body          for (...); or while (...); the semicolon is the whole body                    (medium / low)
//   float_equality           == or != on float / double (x != x, the NaN test, is allowed)                  (medium)
//   unused_variable          a local that nothing reads or writes after its declaration                    (low)
void addSuspiciousCodeRules(RuleList& rules);

} // namespace nc::checks
