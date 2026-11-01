# AST and CFG Schema

Reference for the two AST formats and the control flow graph used by the engine.
Referenced by `test_cfg.cpp`, the complexity analyzers, and the optimizer passes.
Reconstructed from the module contracts; update if T4's implementation diverges.

## Why two AST formats

The engine produces two shapes of the same tree, on purpose:

1. **Simplified AST (v1)** — small, for the UI. Enough to draw a readable tree and
   highlight the nodes an optimization touches. Not used for ML.
2. **Clang-shaped AST** — mirrors libclang node kinds exactly. This is the only
   input to feature extraction, because the 53 v2 features are defined over
   clang node kinds (`features.py` is the source of truth). Must match
   `tools/extract_ast.py` node for node, or features drift from the training data.

Rule: features are computed on the clang-shaped AST of **I/O-stripped** code. User
code with `main`/`cin`/`cout` must pass through the I/O stripper first, since the
training data was stripped the same way.

## Simplified AST (v1) node

```
{
  "kind":     "FunctionDecl" | "ForStmt" | "IfStmt" | "CallExpr" | ...,
  "label":    "for (i = 0; i < n; ++i)",   // human-readable, UI only
  "line":     12,
  "children": [ <node>, ... ]
}
```

## Clang-shaped AST node

```
{
  "kind":     "<libclang CursorKind spelling>",  // e.g. FOR_STMT, CALL_EXPR, ARRAY_SUBSCRIPT_EXPR
  "spelling": "operator[]",                       // cursor spelling where relevant
  "type":     "std::vector<int>",                 // canonical type where relevant
  "children": [ <node>, ... ]
}
```

Notes:
- Vector element access appears as a `CALL_EXPR` with spelling `operator[]` (how
  features 48-52, pointer/index movement, are detected).
- Node kinds and nesting must be identical to `extract_ast.py` output; parity is
  checked by `parity_ast.py` and `test_features.cpp`.

## Control Flow Graph

Built from the clang-shaped AST by `CfgBuilder`.

### Block

```
BasicBlock {
  id:          int
  statements:  [ <ast node ref>, ... ]
  successors:  [ block id, ... ]
  predecessors:[ block id, ... ]
}
```

### Graph

```
Cfg {
  entry()       -> BasicBlock*     // single entry
  exit()        -> BasicBlock*     // single virtual exit (all returns flow here)
  blockCount()  -> size_t
  branchCount() -> size_t          // count of conditional branches
  loopDepth()   -> int             // max nesting of loop back-edges
}
```

Edges:
- Sequential statements -> one successor.
- `if` / `switch` -> one successor per arm; the arms re-join at a merge block.
- Loops (`for`, `while`, `do`) -> a back-edge to the loop header. A back-edge is
  what marks a loop; nesting of back-edges gives `loopDepth()`.
- `return` / `break` / `continue` -> edge to the matching target, not the next
  textual statement. Statements with no path from entry are unreachable.

### Derived analyses

- **Dominators** — `dom.dominates(a, b)` is true if every path from entry to `b`
  passes through `a`. Entry dominates every block.
- **Reachability** — `rc.hasUnreachable()` / `rc.unreachableBlocks()`. Any block
  with no path from entry (for example code after an unconditional `return`).

### How complexity uses it

- `loopDepth() == 0` -> O(1) / O(n) by statement count.
- `loopDepth() == 1` -> O(n).
- `loopDepth() == 2` over the same bound -> O(n^2); the nested-loop shape is also
  what `PairSumToHash` looks for.
- A sorted-container scan at `loopDepth() == 1` is what `LinearToBinarySearch`
  looks for.
