#include "LinearToBinarySearch.hpp"
#include "../SummaryBuilder.hpp"

namespace nc::optimize {

// TODO(T4): replace with real check (single loop scanning a container known to
// be sorted, looking for an equality match / early return).
static bool matchesLinearScan(const PassContext&, std::string& arr, std::string& target) {
    arr = "a";
    target = "k";
    return false;
}

std::optional<OptimizationResult> LinearToBinarySearch::tryApply(const PassContext& ctx) const {
    std::string arr, target;
    if (!matchesLinearScan(ctx, arr, target)) return std::nullopt;

    SummaryBuilder sb;
    OptimizationResult r;
    r.passName = name();
    r.title = "Replace linear scan with binary search";
    r.rationale = "The data is already sorted, so a binary search finds the "
                  "value in logarithmic time instead of scanning every element.";
    r.beforeComplexity = "O(n)";
    r.afterComplexity = "O(log n)";
    r.rewrittenCode = sb.build("linear_to_binary.cpp.tmpl", arr, target);
    return r;
}

} // namespace nc::optimize
