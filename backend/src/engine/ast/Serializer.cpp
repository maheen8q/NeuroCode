#include "engine/ast/Serializer.hpp"

namespace nc::ast {

nlohmann::json toJson(const Node& n) {
    nlohmann::json j;
    j["type"] = n.type;
    j["spelling"] = n.spelling;
    j["line"] = n.line;
    if (n.dtype)      j["dtype"] = *n.dtype;
    if (n.tokens)     j["tokens"] = *n.tokens;
    if (n.op)         j["op"] = *n.op;
    if (n.has_else)   j["has_else"] = *n.has_else;
    if (n.base_text)  j["base_text"] = *n.base_text;
    if (n.index_text) j["index_text"] = *n.index_text;
    j["children"] = nlohmann::json::array();
    for (const auto& c : n.children) j["children"].push_back(toJson(c));
    return j;
}

std::string toJsonString(const Node& n) { return toJson(n).dump(); }

Node fromJson(const nlohmann::json& j) {
    Node n;
    n.type = j.value("type", "");
    n.spelling = j.value("spelling", "");
    n.line = j.value("line", 0);
    if (j.contains("dtype"))      n.dtype = j.at("dtype").get<std::string>();
    if (j.contains("tokens"))     n.tokens = j.at("tokens").get<std::string>();
    if (j.contains("op"))         n.op = j.at("op").get<std::string>();
    if (j.contains("has_else"))   n.has_else = j.at("has_else").get<bool>();
    if (j.contains("base_text"))  n.base_text = j.at("base_text").get<std::string>();
    if (j.contains("index_text")) n.index_text = j.at("index_text").get<std::string>();
    if (j.contains("children"))
        for (const auto& c : j.at("children")) n.children.push_back(fromJson(c));
    return n;
}

} // namespace nc::ast
