#pragma once
#include "../IPass.hpp"

namespace nc::optimize {
class FibonacciMemo : public IPass {
public:
    std::string name() const override { return "FibonacciMemo"; }
    std::optional<OptimizationResult> tryApply(const PassContext& ctx) const override;
};
} // namespace nc::optimize
