// The T4 <-> T1 contract that fails silently if it drifts. Run from backend/ (reads src/ml/labels.json):
//   ./test_ml_contract [path/to/labels.json]
// Frozen: feature vector v2 (53 dims, tools/features.py) and the 12 labels in src/ml/labels.json.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ml/FeatureExtractor.hpp"
#include "ml/StubPredictor.hpp"

using namespace nc;

int main(int argc, char** argv) {
    int bad = 0;
    auto fail = [&](const std::string& msg) { std::cerr << "FAIL " << msg << "\n"; ++bad; };

    // 1. Feature count: one number everywhere. features.py says 53.
    constexpr int kFrozenFeatures = 53;
    StubPredictor sp(1);
    const auto stub_feats = sp.extractFeatures("{}", "{}");
    if (kFeatureCount != kFrozenFeatures)
        fail("IPredictor.hpp kFeatureCount=" + std::to_string(kFeatureCount) + ", frozen is 53 (T1 owns this constant)");
    if (StubPredictor::kFeatureDim != kFrozenFeatures)
        fail("StubPredictor::kFeatureDim=" + std::to_string(StubPredictor::kFeatureDim) + ", frozen is 53");
    if (static_cast<int>(stub_feats.size()) != kFrozenFeatures)
        fail("StubPredictor::extractFeatures().size()=" + std::to_string(stub_feats.size()));
    if (kFeatureVectorLength != kFrozenFeatures)
        fail("FeatureExtractor kFeatureVectorLength=" + std::to_string(kFeatureVectorLength));
    if (static_cast<int>(computeFeatures(ast::Node{}).size()) != kFrozenFeatures)
        fail("FeatureExtractor output length");

    // 2. Labels: exactly labels.json, same order (the order is the class index), and the 12 agreed names.
    const std::vector<std::string> frozen = {
        "binary_search", "brute_force_pair_search", "dp", "graph_traversal",
        "greedy_scan", "hashing", "heap_based", "linear_search", "naive_recursion",
        "sliding_window", "sorting_based", "two_pointer"};
    if (StubPredictor::labels() != frozen) fail("StubPredictor::labels() differs from the 12 agreed labels");

    const std::string path = argc > 1 ? argv[1] : "src/ml/labels.json";
    std::ifstream in(path);
    if (!in) {
        fail("cannot open " + path);
    } else {
        try {
            const auto file_labels = nlohmann::json::parse(in).at("labels").get<std::vector<std::string>>();
            if (file_labels != frozen) fail(path + " differs from the 12 agreed labels");
            if (file_labels != StubPredictor::labels()) fail(path + " differs from StubPredictor::labels()");
        } catch (const std::exception& e) {
            fail(path + ": " + e.what());
        }
    }
    // "unknown" is an API-level answer (api-spec section 9), never a class index.
    if (std::find(frozen.begin(), frozen.end(), "unknown") != frozen.end()) fail("'unknown' must not be a class");

    if (bad == 0) std::cout << "ml contract OK\n";
    return bad ? 1 : 0;
}
