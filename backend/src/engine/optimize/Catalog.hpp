#pragma once
#include "IPass.hpp"
#include <memory>
#include <vector>

namespace nc::optimize {

// Owns the registered passes and runs them in order. The engine holds one
// Catalog and calls run() per job.
class Catalog {
public:
    Catalog();                                  // registers the built-in passes
    void add(std::unique_ptr<IPass> pass);      // for tests / future passes
    // Runs every pass; collects a result from each that matched.
    std::vector<OptimizationResult> run(const PassContext& ctx) const;
    std::size_t size() const { return passes_.size(); }

private:
    std::vector<std::unique_ptr<IPass>> passes_;
};

} // namespace nc::optimize
