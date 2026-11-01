#pragma once
#include "engine/checks/rules/Rule.hpp"

namespace nc::checks {

// Rules about numbers.
//   overflow_risk     (lo + hi) / 2 in int; a product computed in int and then widened to long long; int accumulators
//                     that multiply or add inside loops                                                  (medium / low)
//   division_by_zero  dividing by x.size() or x.size() - 1 with no emptiness check                         (medium)
// The AST does not carry the value of a literal, so `x / 0` is not found: these rules look at types and shapes.
void addArithmeticRules(RuleList& rules);

} // namespace nc::checks
