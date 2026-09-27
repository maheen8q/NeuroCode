#pragma once
#include <string>
#include <vector>
#include <memory>
#include "engine/IEngine.hpp" // for AlgorithmGuess (pushed by engine-ansh)

namespace nc
{

    // Fixed for the life of the project. Changing it invalidates every checkpoint.
    constexpr int kFeatureCount = 48;

    class IPredictor
    {
    public:
        virtual ~IPredictor() = default;

        // ast_json and cfg_json come from the engine
        virtual std::vector<float> extractFeatures(const std::string &ast_json,
                                                   const std::string &cfg_json) = 0;

        // ranked, highest probability first, length <= k
        virtual std::vector<AlgorithmGuess> predict(const std::vector<float> &features,
                                                    int k = 3) = 0;

        virtual bool loadModel(const std::string &checkpoint_path) = 0;
        virtual std::string activeModelVersion() const = 0;
    };

    std::unique_ptr<IPredictor> makeStubPredictor();
    std::unique_ptr<IPredictor> makePredictor(const std::string &models_dir);

} // namespace nc