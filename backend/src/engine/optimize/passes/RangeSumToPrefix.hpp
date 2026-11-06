#pragma once
#include "../IPass.hpp"

namespace nc::optimize {
class RangeSumToPrefix : public IPass {
public:
    std::string name() const override { return "RangeSumToPrefix"; }
    std::optional<OptimizationResult> tryApply(const PassContext& ctx) const override;
};
} // namespace nc::optimize
