# NeuroCode

An AI-assisted C++ code reasoning and optimization engine.

A user submits a C++ solution plus a short structured description of the problem it solves. NeuroCode parses the code into an AST, builds a control flow graph, derives its time and space complexity by rule, classifies the algorithm using a trained neural network, runs the code in a sandbox against generated test cases against a reference solution, and, when a known faster pattern applies, produces an optimized version with a measured, verified speedup.

It is not a linter and not a chatbot wrapper. Every layer is built in this repository, and nothing is shown to the user until it has actually been checked.

---
## 🚀 Live Demo

- **App:** [https://neurocode-xi.vercel.app/](https://your-app.vercel.app)
- **API:** [https://neurocode-e8na.onrender.com/api/v1/health](https://neurocode-e8na.onrender.com/api/v1/health)

> Note: the backend runs on Render's free tier, so the first request after idle can take ~50 seconds.

## What it does, concretely

**Input**

```cpp
bool solve(vector<int>& a, int k) {
    for (int i = 0; i < a.size(); i++)
        for (int j = i + 1; j < a.size(); j++)
            if (a[i] + a[j] == k) return true;
    return false;
}
```
Problem metadata: input = integer array, n ≤ 100000, output = boolean, operation = pair sum.

**Output, part 1: analysis**

| Field | Value |
|---|---|
| Detected algorithm (classifier) | Brute force pair search (99.9%) |
| Time complexity (rule-derived) | O(n²) |
| Space complexity (rule-derived) | O(1) |
| Verdict | Fails on inputs near `INT_MAX`: `a + b` overflows a 32-bit `int` |
| Recommended | Hash set lookup, O(n) time / O(n) space |

The classifier's guess and the complexity engine's derivation are computed independently. They do not have to agree, and when they don't, the rule-derived result is the one the UI tells the user to trust. The classifier is a trained signal, not a proof.

**Output, part 2: optimized code**

```cpp
bool solve(vector<int>& a, int k) {
    unordered_set<long long> seen;
    for (int x : a) {
        if (seen.count(static_cast<long long>(k) - x)) return true;
        seen.insert(x);
    }
    return false;
}
```

**Output, part 3: summary of what changed and why**

> **Replaced the inner scan with a hash set lookup.**
>
> The original checks every pair, so the work grows with the square of the input size. The new version stores each value as it passes and asks the set whether the complement `k - x` has already been seen, which answers the same question in one pass, and uses a 64-bit accumulator so the complement calculation can't silently wrap around the way the original's addition does.
>
> | | Original | Optimized |
> |---|---|---|
> | Algorithm | Brute force pair search | Hash set lookup |
> | Time | O(n²) | O(n) expected |
> | Space | O(1) | O(n) |
> | Measured speedup at n = 20000 | | **~13x** |
>
> **Verification:** compiled successfully and matched the reference answer on every generated input, including the overflow cases the original code failed. Unverified code is never shown; if the generated candidate had not matched, the user would see "no verified optimization found" instead.

---

## Four verified optimizations

The optimizer is a catalog of independent passes. Each one pattern-matches a specific inefficient shape in the AST, generates a rewrite from a template, and only shows that rewrite if it compiles and produces identical output to the user's original on a battery of generated tests, including edge cases the user's own code may not have considered. A pass that cannot verify its rewrite shows nothing.

| Pass | Detects | Rewrites to | Measured on this build |
|---|---|---|---|
| `PairSumToHash` | Nested-loop pair-sum scan | Hash-set lookup | O(n²) → O(n), ~13x at n = 20000 |
| `LinearToBinarySearch` | Linear scan of a declared-sorted array | `std::binary_search` | O(n) → O(log n) |
| `RangeSumToPrefix` | Per-query range summation | Prefix-sum array | O(n·q) → O(n+q) |
| `FibonacciMemo` | Naive doubly-recursive Fibonacci | Iterative, bottom-up table | O(2ⁿ) → O(n) |

A fifth catalog entry, k smallest elements, has full differential-testing support (a real reference implementation and an order-independent correctness check) but no optimizer pass yet; it is correctly analyzed but never offered an optimization, which is the intended behavior for any pattern the catalog doesn't yet cover.

---

## The classifier

A feedforward network (53 hand-engineered AST/CFG features, two hidden layers, trained with cross-validation) predicts which of twelve algorithmic shapes the submitted code most resembles, returned as a ranked, probability-weighted list, never a single confident label. It is informational context, not a source of truth: the complexity and optimization results never depend on it.

---

## Stack

| Layer | Technology | Reason |
|---|---|---|
| Frontend | React 18 + TypeScript + Vite | |
| Editor | Monaco Editor | Same editor as VS Code, includes a diff view |
| Styling | Tailwind CSS | |
| Backend | C++20 + Drogon | Async HTTP and WebSocket in one framework |
| Parsing | libclang | Real Clang AST, no hand-written parser |
| ML | LibTorch (PyTorch C++ API) | Training and inference both happen inside the C++ backend |
| Database | SQLite | |
| Sandbox | Forked subprocess with POSIX rlimits | Untrusted user code never runs in the API process |
| Tests | GoogleTest (C++), Vitest (frontend) | |

---

## Repository layout

```
neurocode/
├── README.md
├── docs/
│   ├── architecture.md          system design, contracts, DB schema
│   └── api-spec.md              every endpoint and JSON shape
├── frontend/                    React + TypeScript
└── backend/                     C++20, Drogon, LibTorch
```

---

## Team and ownership

Four people, five weeks.

| Owner | Lane | Folders |
|---|---|---|
| Maheen | ML: features, dataset, LibTorch model, training, inference; also the final service-wiring, optimizer pass detection, and differential-testing additions in this repository | `backend/src/ml/`, `backend/data/`, `backend/models/` |
| Mokshi | Frontend: all UI, editor, charts, pages | `frontend/` |
| Wadia | Backend web side: API, WebSocket, auth, DB, job orchestration | `backend/src/api/`, `services/`, `storage/`, `db/` |
| Anshrah | Backend compute side: AST, CFG, complexity, optimizer, sandbox | `backend/src/engine/`, `backend/src/sandbox/` |

---

## Getting started

### Prerequisites

- CMake 3.20+, a C++20 compiler (GCC 11+ or Clang 14+)
- Drogon 1.9+, SQLite3, nlohmann/json, libsodium, libclang-dev
- LibTorch 2.x (CPU build is fine)
- Node.js 18+

Linux or WSL strongly recommended.

### Backend

```bash
cd backend
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/libtorch
cmake --build build -j
./build/neurocode        # serves on http://localhost:8080
```

### Frontend

```bash
cd frontend
npm install
npm run dev              # serves on http://localhost:5173
```

### Verify the stack

```bash
curl http://localhost:8080/api/v1/health
```

---

## Scope boundaries

**In scope**
- C++ input only
- Structured problem metadata, not free-text natural language understanding
- A catalog of classic problem patterns for oracle-based verification: pair sum, sorted search, range sum, Fibonacci, k smallest elements
- One trained model: algorithm classification from AST/CFG features, informational only
- Four verified, template-based optimizer passes
- Static findings from AST and CFG analysis (overflow-prone arithmetic, unreachable code, unused variables)

**Out of scope**
- Natural language problem parsing
- Free-form neural code generation — the optimizer only recognizes and rewrites known patterns, it does not invent new algorithms
- Multi-file projects, external libraries beyond the STL
- Proving correctness. Differential testing finds counterexamples; it never proves their absence
- Languages other than C++

**Known gaps, stated plainly**
- Only four of the five catalog problems have an implemented optimizer pass; k smallest elements is differentially tested but has no rewrite rule yet
- The server does not yet gracefully shut down (`pure virtual method called` on interrupt); this does not affect normal operation
- Job history is kept in memory only and is lost on server restart

---

## Honesty policy for results

1. Model outputs are always shown as a ranked list with probabilities, never as a single confident label, and are clearly distinguished from the rule-derived complexity result.
2. A bug is only reported as a bug when a counterexample exists. Anything else is labelled "suspicious pattern".
3. Speedups are measured on real runs against the user's own original code, never quoted from complexity classes alone.
4. An optimized rewrite is never shown unless it has compiled and matched the reference answer on every generated test.

---

## Documentation

- [`docs/architecture.md`](docs/architecture.md)
- [`docs/api-spec.md`](docs/api-spec.md)