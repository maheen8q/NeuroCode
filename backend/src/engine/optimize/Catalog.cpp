#include "Catalog.hpp"
#include "passes/PairSumToHash.hpp"
#include "passes/LinearToBinarySearch.hpp"
#include "passes/RangeSumToPrefix.hpp"

namespace nc::optimize {

Catalog::Catalog() {
    add(std::make_unique<PairSumToHash>());
    add(std::make_unique<LinearToBinarySearch>());
    add(std::make_unique<RangeSumToPrefix>());
}

void Catalog::add(std::unique_ptr<IPass> pass) {
    if (pass) passes_.push_back(std::move(pass));
}

std::vector<OptimizationResult> Catalog::run(const PassContext& ctx) const {
    std::vector<OptimizationResult> out;
    for (const auto& p : passes_) {
        if (auto r = p->tryApply(ctx)) out.push_back(std::move(*r));
    }
    return out;
}

} // namespace nc::optimize
