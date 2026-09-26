#pragma once
#include <atomic>
#include "IEngine.hpp"

namespace nc {

// Week-1 stub. Returns a fixed, valid result immediately (after fake progress
// steps) so T3 can build the API and T2 can build the UI without waiting on
// libclang or LibTorch. Swapped for the real orchestration in week 2; the
// IEngine surface does not change when that happens.
class Engine : public IEngine {
public:
    AnalysisResult analyze(const std::string& code,
                           const ProblemSpec& spec,
                           ProgressFn on_progress) override;

    OptimizeResult optimize(const std::string& code,
                            const ProblemSpec& spec,
                            ProgressFn on_progress) override;

    void cancel() override;

private:
    std::atomic<bool> cancelled_{false};

    AnalysisResult fakeAnalysis(const ProblemSpec& spec, ProgressFn on_progress);
};

} // namespace nc