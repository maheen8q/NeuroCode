#include "engine/ast/Traversal.hpp"

#include <algorithm>
#include <deque>

namespace nc::ast {

namespace {

void pre(const Node& n, int depth, const VisitFn& f) {
    f(n, depth);
    const int inner = depth + (isLoop(n.type) ? 1 : 0);
    for (const auto& c : n.children) pre(c, inner, f);
}

void post(const Node& n, int depth, const VisitFn& f) {
    const int inner = depth + (isLoop(n.type) ? 1 : 0);
    for (const auto& c : n.children) post(c, inner, f);
    f(n, depth);
}

void functions(const Node& n, std::vector<const Node*>& out) {
    if (isFunctionLike(n.type) && bodyOf(n)) {
        out.push_back(&n);
        return;
    }
    for (const auto& c : n.children) functions(c, out);
}

} // namespace

bool isLoop(const std::string& t) {
    return t == "FOR_STMT" || t == "WHILE_STMT" || t == "DO_STMT" || t == "CXX_FOR_RANGE_STMT";
}

bool isFunctionLike(const std::string& t) {
    return t == "FUNCTION_DECL" || t == "CXX_METHOD" || t == "CONSTRUCTOR" || t == "DESTRUCTOR" ||
           t == "FUNCTION_TEMPLATE";
}

void dfsPreorder(const Node& root, const VisitFn& f) { pre(root, 0, f); }
void dfsPostorder(const Node& root, const VisitFn& f) { post(root, 0, f); }

void bfs(const Node& root, const VisitFn& f) {
    std::deque<std::pair<const Node*, int>> q;
    q.emplace_back(&root, 0);
    while (!q.empty()) {
        const auto [n, depth] = q.front();
        q.pop_front();
        f(*n, depth);
        const int inner = depth + (isLoop(n->type) ? 1 : 0);
        for (const auto& c : n->children) q.emplace_back(&c, inner);
    }
}

std::vector<const Node*> collect(const Node& root, const std::string& type) {
    std::vector<const Node*> out;
    dfsPreorder(root, [&](const Node& n, int) { if (n.type == type) out.push_back(&n); });
    return out;
}

std::vector<const Node*> functionsOf(const Node& root) {
    std::vector<const Node*> out;
    functions(root, out);
    return out;
}

const Node* bodyOf(const Node& fn) {
    for (auto it = fn.children.rbegin(); it != fn.children.rend(); ++it)
        if (it->type == "COMPOUND_STMT") return &*it;
    return nullptr;
}

int countOf(const Node& root, const std::string& type) {
    int n = 0;
    dfsPreorder(root, [&](const Node& x, int) { if (x.type == type) ++n; });
    return n;
}

int maxLoopDepth(const Node& root) {
    int deepest = 0;
    dfsPreorder(root, [&](const Node& n, int depth) {
        if (isLoop(n.type)) deepest = std::max(deepest, depth + 1);
    });
    return deepest;
}

bool containsCall(const Node& root, const std::string& callee) {
    bool found = false;
    dfsPreorder(root, [&](const Node& n, int) {
        if (n.type == "CALL_EXPR" && n.spelling == callee) found = true;
    });
    return found;
}

int cyclomatic(const Node& root) {
    int c = 1;
    dfsPreorder(root, [&](const Node& n, int) {
        if (n.type == "IF_STMT" || n.type == "CONDITIONAL_OPERATOR" || n.type == "SWITCH_STMT" || isLoop(n.type)) ++c;
    });
    return c;
}

} // namespace nc::ast
