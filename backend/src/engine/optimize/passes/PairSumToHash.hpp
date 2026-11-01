#pragma once
#include "../IPass.hpp"

namespace nc::optimize {
// O(n^2) nested-loop pair search -> O(n) hash set.
class PairSumToHash : public IPass {
public:
    std::string name() const override { return "PairSumToHash"; }
    std::optional<OptimizationResult> tryApply(const PassContext& ctx) const override;
};
} // namespace nc::optimize
