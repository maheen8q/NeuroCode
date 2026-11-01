#pragma once
#include <vector>

#include "engine/ast/Node.hpp"
#include "engine/cfg/Cfg.hpp"

namespace nc::cfg {

// Builds control flow graphs from the internal AST (raw clang cursor kinds, see ast/Node.hpp), in one
// recursive pass per function. The construction rules below DEFINE cfg_nodes / cfg_edges / branch counts;
// nothing in the 53-feature vector reads the CFG (cyclomatic there is decision points + 1), but the UI,
// Reachability and the static checks do.
//
//   function            synthetic `entry` block, one synthetic `exit` block (no lines)
//   straight-line code  a run of statements shares one `basic` block
//   if / else if / else a `branch` block, `true` and `false` edges, both arms rejoin at the next block
//   for                 init joins the current block; `loop_cond` (true -> body, false -> after);
//                       body; `loop_inc` with a `back` edge to loop_cond
//   while, range-for    `loop_cond`; the end of the body has a `back` edge to loop_cond; no loop_inc
//   do-while            body first, then `loop_cond` with a `back` edge to the body start, false -> after
//   switch              a `branch` block with a `case` edge per label, a `default` edge (to the default
//                       label, or to after the switch if there is none); break -> after; fall-through
//                       -> next label
//   statement ternary   `c ? a : b;` as a whole statement: a `branch` block and two arm blocks
//   && and ||           do NOT split blocks
//   return, throw       its own `return` block with an edge to `exit`
//   break / continue    break -> after the loop or switch; continue -> loop_inc for `for`, else loop_cond
//   goto, try/catch     unsupported: Cfg::unsupported is set and no graph is built
//
// Limits: while(true) / for(;;) are detected as infinite (no exit edge); a do-while is always treated as
// finite; calls that never return (exit(), abort()) are ordinary statements. `line` is the line libclang
// reports for the cursor (the `if`/`for` keyword, the start of a statement, the operator of a binary one).
std::vector<Cfg> buildCfgs(const ast::Node& root);   // one per function with a body, source order
Cfg buildCfg(const ast::Node& function);             // a FUNCTION_DECL / CXX_METHOD / ... with a body

} // namespace nc::cfg
