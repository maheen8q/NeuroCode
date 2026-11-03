# NeuroCode frontend (T2)

React 18, TypeScript, Vite, Tailwind. Monaco for code, Recharts for charts.
Contract: `docs/api-spec.md` (v1.0). Types mirrored by hand in `src/types/api.d.ts`.

## Run

```bash
npm install
cp .env.example .env     # VITE_USE_MOCK=true needs no backend
npm run dev              # http://localhost:5173
```

Mock mode serves the exact response shapes from the spec. Any username (3 to 32 chars) and password (8+) signs in.
Code containing `syntax_error` fails with `COMPILE_ERROR`, which is handy for testing error states.

**Real backend:** set `VITE_USE_MOCK=false`. Vite proxies `/api` (REST and the WebSocket) to `http://localhost:8080`.

## Pages

| Route | File | Spec section |
|---|---|---|
| `/login` | `pages/Login.tsx` | 3 Auth |
| `/analyze` | `pages/Analyzer.tsx` | 4 Problems, 5 Jobs, 7 Execute |
| `/jobs/:id` | `pages/Results.tsx` | 5 Jobs, 6 WebSocket |
| `/optimize/:id` | `pages/Optimization.tsx` | 5 Jobs, rendering rules |
| `/history` | `pages/History.tsx` | 5 `GET /jobs` (server-side filter and paging) |
| `/training` | `pages/Training.tsx` | 6 epoch events, 8 Models |

The sidebar shows `GET /health`: server status, active model version, queue depth.

## Rules the UI follows

From the spec's rendering rules and the README honesty policy:

- Unverified generated code is never rendered. `verified: false` shows "No verified optimization found" plus the analysis.
- Speedup is hidden when `summary.speedup` is 0. An empty trade-off renders "No significant trade-off".
- The summary and the diff have equal weight on the optimization page.
- Algorithm predictions are always a ranked list with probabilities, with the server's `low_confidence` flag shown.
- Static findings are labelled "Suspicious patterns". Only counterexamples are called confirmed failures.
- Screens that report test results say that testing does not prove correctness.
- Progress: WebSocket for live updates, plus `GET /jobs/{id}` every 2 s as the fallback the spec requires.

## Open points for T3 (things the spec leaves out)

1. **`GET /jobs/{id}` returns neither the submitted code nor the problem.** The UI keeps its own copy in `localStorage` (`lib/jobStore.ts`) for the code tab, the diff, "Edit and re-run" and "Optimize this code". Jobs submitted from another browser show a notice instead. Adding `code` and `problem_id` to the response would remove this workaround.
2. **No result shape for `type: "train"` jobs.** The loss chart is built from live `epoch` events only, so it is empty after a reload. A per-epoch history in the job result would fix that.
3. **`docs/api-spec.md` contains unresolved merge-conflict markers and three copies of the spec.** The copies differ on `GET /models` (`cv_accuracy` and nullable `loss` versus `accuracy`) and only one has `GET /models/{id}/metrics`. The UI follows the newer copy and accepts either field name. Please resolve the file.
4. The shapes of `ast` and `cfg` nodes beyond `{type, name, children}` and `{nodes, edges}` are not defined. The viewers accept `name` or `label`, `from`/`to` or `source`/`target`, and `kind` or `type`.
