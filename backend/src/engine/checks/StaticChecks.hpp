#pragma once
#include <set>
#include <string>
#include <vector>

#include "engine/AnalysisResult.hpp"
#include "engine/ast/Node.hpp"
#include "engine/cfg/Cfg.hpp"

namespace nc::checks {

// Static checks over the user's code: bugs and suspicious constructs found without running it. Each finding is
// {line, severity "high" | "medium" | "low", type, message}, exactly the api-spec `findings` entry.
//
// Finding types (the id of the rule that reported it):
//   unreachable, missing_return, infinite_loop, infinite_recursion      control flow, built on the CFG
//   overflow_risk, division_by_zero                                     numbers
//   out_of_bounds, uninitialized_variable, memory_leak                  memory
//   assignment_in_condition, empty_loop_body, float_equality, unused_variable          code that is probably not what was meant
//
// The checks are heuristics that look at types and shapes, never at values (the AST does not carry literal values), and
// they are tuned to stay quiet on correct code: a missed bug costs less than a false alarm on a working solution. The one
// deliberate exception is the low-severity overflow_risk hint on an int that sums values in a loop.

struct CheckOptions {
    std::set<std::string> disabled;     // rule ids to skip
    size_t max_findings = 100;          // the list is cut after sorting, so the most severe ones stay
};

// Canonical rule ids, in the order the rules run.
std::vector<std::string> ruleIds();

// Runs every rule on every function of `root` (functions with a body). The control flow graphs and loop analysis
// are built here; use the overload below when the engine already has the graphs.
std::vector<Finding> runChecks(const ast::Node& root, const CheckOptions& options = {});

// Same, with the graphs the caller built (cfg::buildCfgs(root)). A function is matched to its graph by name and line;
// a function without one is checked without control flow rules.
std::vector<Finding> runChecks(const ast::Node& root, const std::vector<cfg::Cfg>& cfgs, const CheckOptions& options = {});

// Sorted by severity (high first), then line, then type; identical findings (same line, type and message) appear once.
void sortFindings(std::vector<Finding>& findings);

} // namespace nc::checks
