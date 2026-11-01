#pragma once
#include <array>
#include <string>
#include <vector>

#include "engine/ast/Node.hpp"

namespace nc {

// The 53-dimension feature vector, version 2. C++ mirror of backend/tools/features.py, which is the
// source of truth: same order, same counting rules, including its quirks (see FeatureExtractor.cpp).
// FROZEN: order and length are tied to feature_mean / feature_std in the model sidecar. Do not edit
// anything here without the same change in features.py and a retrained checkpoint.
//
// Depends only on engine/ast/Node.hpp and nlohmann (no LibTorch), so it can live in the engine build.
constexpr int kFeatureVectorLength = 53;

// FEATURE_NAMES from features.py, in order.
const std::array<const char*, kFeatureVectorLength>& featureNames();

// AST (as produced by parse::buildAst) -> 53 floats. Pure function, thread safe.
std::vector<float> computeFeatures(const ast::Node& root);

// Same, from AST JSON text (the shape tools/extract_ast.py and ast::toJson write).
// Throws std::runtime_error if the text is not valid AST JSON.
std::vector<float> computeFeaturesFromJson(const std::string& ast_json);

} // namespace nc
