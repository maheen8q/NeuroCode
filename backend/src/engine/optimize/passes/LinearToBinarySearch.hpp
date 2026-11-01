#pragma once
#include "../IPass.hpp"

namespace nc::optimize {
// O(n) scan over sorted data -> O(log n) binary search.
class LinearToBinarySearch : public IPass {
public:
    std::string name() const override { return "LinearToBinarySearch"; }
    std::optional<OptimizationResult> tryApply(const PassContext& ctx) const override;
};
} // namespace nc::optimize
