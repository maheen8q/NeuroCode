#pragma once
#include <string>

#include <nlohmann/json.hpp>

#include "engine/ast/Node.hpp"

namespace nc::ast {

// Same JSON that tools/extract_ast.py writes: optional fields are left out, not null.
nlohmann::json toJson(const Node& node);
std::string toJsonString(const Node& node);

// Inverse of toJson, for AST JSON that arrives as text (IPredictor::extractFeatures gets a string).
// Missing optional fields stay unset; a missing "children" is an empty list. Throws
// nlohmann::json::exception on malformed input.
Node fromJson(const nlohmann::json& j);

} // namespace nc::ast
