#include "engine/Engine.hpp"
#include "engine/IEngine.hpp"

namespace nc {

// The real engine. EngineStub stays in the tree for the API tests that rely on its STUB: hooks.
std::unique_ptr<IEngine> makeEngine(IPredictor& predictor) {
    return std::make_unique<Engine>(predictor);
}

} // namespace nc
