#pragma once
#include <functional>
#include <string>
#include <vector>

#include "engine/ast/Node.hpp"

namespace nc::ast {

// Traversals and queries over the internal AST (ast::Node, raw clang cursor-kind names such as
// FOR_STMT). architecture.md section 10: the AST is an N-ary tree, walked depth-first or breadth-first
// through a visitor. The CFG builder, complexity engine and static checks all go through here instead
// of each re-implementing the walk.

// callback(node, loop_depth): loop_depth = number of loops enclosing the node. A loop node itself
// reports the depth OUTSIDE it, so a top-level loop reports 0 and its body reports 1.
using VisitFn = std::function<void(const Node&, int loop_depth)>;

void dfsPreorder(const Node& root, const VisitFn& f);    // parent before children, source order
void dfsPostorder(const Node& root, const VisitFn& f);   // children before parent
void bfs(const Node& root, const VisitFn& f);            // level by level

bool isLoop(const std::string& type);          // FOR_STMT, WHILE_STMT, DO_STMT, CXX_FOR_RANGE_STMT
bool isFunctionLike(const std::string& type);  // FUNCTION_DECL, CXX_METHOD, CONSTRUCTOR, DESTRUCTOR, FUNCTION_TEMPLATE

// All nodes of one cursor kind, preorder.
std::vector<const Node*> collect(const Node& root, const std::string& type);

// Every function-like node that has a body, in source order, found anywhere under root (namespaces,
// classes, ...). Does not descend into a function, so lambdas and local classes are not listed
// separately: their code belongs to the enclosing function.
std::vector<const Node*> functionsOf(const Node& root);

// The COMPOUND_STMT body of a function-like node, or nullptr for a declaration without one.
const Node* bodyOf(const Node& function);

int countOf(const Node& root, const std::string& type);
int maxLoopDepth(const Node& root);                       // 0 = no loops, 2 = doubly nested
bool containsCall(const Node& root, const std::string& callee);   // any CALL_EXPR with this spelling

// Decision points + 1: ifs + loops + ternaries + switches + 1. The same definition as the
// "cyclomatic" feature in tools/features.py (group decision), counted over root's subtree.
int cyclomatic(const Node& root);

} // namespace nc::ast
