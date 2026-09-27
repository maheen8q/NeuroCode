#include "ml/IPredictor.hpp"

namespace nc
{

    class StubPredictor : public IPredictor
    {
    public:
        std::vector<float> extractFeatures(const std::string &, const std::string &) override
        {
            return std::vector<float>(kFeatureCount, 0.0f);
        }

        std::vector<AlgorithmGuess> predict(const std::vector<float> &, int k) override
        {
            std::vector<AlgorithmGuess> out = {
                {"brute_force_pair_search", 0.62f},
                {"two_pointer", 0.24f},
                {"linear_scan", 0.14f}};
            if (k < (int)out.size())
                out.resize(k);
            return out;
        }

        bool loadModel(const std::string &) override { return true; }
        std::string activeModelVersion() const override { return "stub"; }
    };

    std::unique_ptr<IPredictor> makeStubPredictor()
    {
        return std::make_unique<StubPredictor>();
    }

    std::unique_ptr<IPredictor> makePredictor(const std::string &)
    {
        return makeStubPredictor(); // replaced in week 3
    }

} // namespace nc