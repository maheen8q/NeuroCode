#include "PairSumToHash.hpp"
#include "../SummaryBuilder.hpp"

namespace nc::optimize {

// TODO(T4): replace with real AST/CFG check (two nested loops over the same
// container, inner index starts at i+1, comparing a[i]+a[j] to a target).
// Returning false keeps the pass silent until detection is wired.
static bool matchesPairSum(const PassContext&, std::string& arr, std::string& target) {
    arr = "a";       // T4: pull the detected container name
    target = "k";    // T4: pull the detected target name
    return false;    // flip to the detection result once implemented
}

std::optional<OptimizationResult> PairSumToHash::tryApply(const PassContext& ctx) const {
    std::string arr, target;
    if (!matchesPairSum(ctx, arr, target)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace nested loop with hash lookup";
    r.rationale = "Each element is checked against a hash set in O(1), "
                  "so the pair search drops from quadratic to linear time.";
    r.beforeComplexity = "O(n^2)";
    r.afterComplexity = "O(n)";
    r.rewrittenCode = sb.build("pair_sum_hash.cpp.tmpl", arr, target);
    return r;
}

} // namespace nc::optimize
