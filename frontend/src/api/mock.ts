// Mock server that returns exactly the shapes in docs/api-spec.md. Enable with VITE_USE_MOCK=true.
// Try: code containing "syntax_error" fails with COMPILE_ERROR; a password under 8 chars fails validation.
import type {
  AnalysisResult, EpochMetrics, Health, Job, JobRow, JobType, ModelInfo, OptimizeResult, Problem, TrainRequest, WsEvent,
} from '../types/api';
import { ApiError } from './ApiError';

const wait = (ms = 200) => new Promise<void>((r) => setTimeout(r, ms));
const T0 = Date.now();

const PROBLEMS: Problem[] = [
  { id: 'pair_sum', title: 'Pair with given sum', input_type: 'int_array', output_type: 'bool', operation: 'pair_sum', max_n: 100000, optimal_time: 'O(n)', statement: 'Given an integer array and a target k, return true if two different elements add up to k.' },
  { id: 'search', title: 'Search in sorted array', input_type: 'int_array', output_type: 'int', operation: 'search', max_n: 1000000, optimal_time: 'O(log n)', statement: 'Return the index of target in a sorted array, or -1 if it is absent.' },
  { id: 'range_sum', title: 'Range sum queries', input_type: 'int_array', output_type: 'int', operation: 'range_sum', max_n: 200000, optimal_time: 'O(1) per query', statement: 'Answer many queries for the sum of a[l..r].' },
  { id: 'scheduling', title: 'Task scheduling by deadline', input_type: 'int_array', output_type: 'array', operation: 'scheduling', max_n: 50000, optimal_time: 'O(n log n)', statement: 'Repeatedly take the task with the smallest deadline.' },
];

let models: ModelInfo[] = [
  { id: 1, version: 'v1-20260320', cv_accuracy: 0.71, loss: null, epochs: 40, dataset_size: 1200, trained_at: '2026-03-20T10:00:00Z', is_active: false },
  { id: 2, version: 'v2-20260402', cv_accuracy: 0.83, loss: null, epochs: 8, dataset_size: 1840, trained_at: '2026-04-02T10:00:00Z', is_active: false },
  { id: 3, version: 'v20261028-0013', cv_accuracy: 0.849, loss: null, epochs: 7, dataset_size: 2067, trained_at: '2026-10-28T00:14:01Z', is_active: true },
];
const activeVersion = () => models.find((m) => m.is_active)?.version ?? 'v3-20260410';

interface Rec { job: Job; started: number; duration: number; code: string; problem_id: string | null; train?: TrainRequest; fail?: boolean; modelAdded?: boolean }
const recs: Rec[] = [];
let seq = 1;

const AST = { type: 'Function', name: 'hasPairSum', line: 4, children: [
  { type: 'Declaration', name: 'a: const vector<int>&', line: 4 }, { type: 'Declaration', name: 'k: int', line: 4 },
  { type: 'Loop', name: 'for i', line: 5, children: [{ type: 'Loop', name: 'for j = i + 1', line: 6, children: [
    { type: 'Branch', name: 'if a[i] + a[j] == k', line: 7, children: [{ type: 'ArrayAccess', name: 'a[i]', line: 7 }, { type: 'ArrayAccess', name: 'a[j]', line: 7 }, { type: 'Return', name: 'true', line: 7 }] }] }] },
  { type: 'Return', name: 'false', line: 10 }] };
const CFG = {
  nodes: [
    { id: 0, label: 'entry', kind: 'entry' }, { id: 1, label: 'i = 0', kind: 'block', line: 5 }, { id: 2, label: 'i < n ?', kind: 'loop', line: 5 },
    { id: 3, label: 'j = i + 1', kind: 'block', line: 6 }, { id: 4, label: 'j < n ?', kind: 'loop', line: 6 }, { id: 5, label: 'a[i] + a[j] == k ?', kind: 'branch', line: 7 },
    { id: 6, label: 'return true', kind: 'block', line: 7 }, { id: 7, label: 'j++', kind: 'block', line: 6 }, { id: 8, label: 'i++', kind: 'block', line: 5 },
    { id: 9, label: 'return false', kind: 'block', line: 10 }, { id: 10, label: 'exit', kind: 'exit' },
  ],
  edges: [
    { from: 0, to: 1 }, { from: 1, to: 2 }, { from: 2, to: 3, label: 'yes' }, { from: 2, to: 9, label: 'no' }, { from: 3, to: 4 },
    { from: 4, to: 5, label: 'yes' }, { from: 4, to: 8, label: 'no' }, { from: 5, to: 6, label: 'yes' }, { from: 5, to: 7, label: 'no' },
    { from: 7, to: 4 }, { from: 8, to: 2 }, { from: 6, to: 10 }, { from: 9, to: 10 },
  ],
};

function analysisFor(code: string): AnalysisResult {
  const loops = (code.match(/\b(for|while)\b/g) || []).length;
  const fib = (code.match(/fib\(/g) || []).length >= 3;
  const base = { model_version: activeVersion(), ast: AST, cfg: CFG, execution: { compiled: true, compile_error: null, tests_run: 200, tests_passed: 200 } };
  if (fib) return { ...base, algorithms: [{ label: 'naive_recursion', probability: 0.81 }, { label: 'dp', probability: 0.12 }, { label: 'greedy_scan', probability: 0.04 }], low_confidence: false,
    complexity: { time: 'O(2^n)', space: 'O(n)', derivation: ['two recursive calls, each on n - 1 (line 3)', 'recurrence T(n) = 2T(n - 1) + O(1)', 'solves to O(2^n)', 'recursion depth n gives O(n) stack space'] },
    findings: [{ line: 3, severity: 'medium', type: 'overflow_risk', message: 'fib(n) exceeds 64 bits for n > 92.' }], counterexamples: [], optimization_available: true };
  if (/\bmid\b/.test(code)) return { ...base, algorithms: [{ label: 'binary_search', probability: 0.93 }, { label: 'two_pointer', probability: 0.05 }, { label: 'greedy_scan', probability: 0.01 }], low_confidence: false,
    complexity: { time: 'O(log n)', space: 'O(1)', derivation: ['loop halves the range [lo, hi] each iteration', 'at most log2(n) iterations', 'only scalar variables declared'] },
    findings: [], counterexamples: [], optimization_available: false };
  if (loops >= 2) return { ...base, execution: { compiled: true, compile_error: null, tests_run: 200, tests_passed: 199 },
    algorithms: [{ label: 'brute_force_pair_search', probability: 0.94 }, { label: 'two_pointer', probability: 0.04 }, { label: 'sliding_window', probability: 0.02 }], low_confidence: false,
    complexity: { time: 'O(n^2)', space: 'O(1)', derivation: ['outer loop runs n times (line 5)', 'inner loop runs n - i - 1 times (line 6)', 'sum over i gives n(n-1)/2', 'no heap allocation detected'] },
    findings: [{ line: 7, severity: 'medium', type: 'overflow_risk', message: 'a[i] + a[j] may overflow int for values near INT_MAX' }, { line: 5, severity: 'low', type: 'off_by_one_suspect', message: 'size_t index compared against a signed value' }],
    counterexamples: [{ input: 'a = [2147483647, 1]\nk = -2147483648', expected_output: 'false', actual_output: 'true' }], optimization_available: true };
  return { ...base, algorithms: [{ label: 'greedy_scan', probability: 0.41 }, { label: 'sliding_window', probability: 0.33 }, { label: 'hashing', probability: 0.12 }], low_confidence: true,
    complexity: { time: 'O(n)', space: 'O(1)', derivation: ['single loop from 0 to n', 'constant work per iteration'] },
    findings: [{ line: 5, severity: 'low', type: 'unused_variable', message: "variable 'tmp' is never read" }], counterexamples: [], optimization_available: false };
}

function optimizeFor(code: string): OptimizeResult {
  const a = analysisFor(code);
  const fib = a.complexity.time === 'O(2^n)';
  if (!a.optimization_available) return { ...a, optimization: { original_algorithm: a.algorithms[0].label, original_complexity: a.complexity.time, recommended_algorithm: '', recommended_complexity: '', optimized_code: null, summary: null, verified: false, verification: null, benchmark: null } };
  const b = fib ? { n_used: 45, before_ms: 1840, after_ms: 0.04, speedup: 46000, before_kb: 2048, after_kb: 2200 } : { n_used: 50000, before_ms: 2841.3, after_ms: 8.4, speedup: 338.2, before_kb: 412, after_kb: 2180 };
  return { ...a, optimization: {
    original_algorithm: a.algorithms[0].label, original_complexity: a.complexity.time, recommended_algorithm: fib ? 'dp' : 'hash_set_lookup', recommended_complexity: 'O(n)',
    optimized_code: fib ? 'long long fib(int n) {\n    vector<long long> memo(n + 1, -1);\n    return fibMemo(n, memo);\n}\n' : 'bool solve(vector<int>& a, int k) {\n    unordered_set<int> seen;\n    for (int x : a) {\n        if (seen.count(k - x)) return true;\n        seen.insert(x);\n    }\n    return false;\n}\n',
    verified: true, verification: { tests_run: 200, mismatches: 0 }, benchmark: b,
    summary: {
      headline: fib ? 'Cached each fib(n) result so it is computed once.' : 'Replaced the inner scan with a hash set lookup.',
      explanation: fib ? 'The naive version recomputes the same subproblems many times. Storing each result in a table means every value is computed once.' : 'The original checks every pair, so the work grows with the square of the input size. The new version stores each value as it passes and asks the set whether the complement k - x has already been seen, which answers the same question in one pass.',
      comparison: [{ label: 'Time', before: a.complexity.time, after: 'O(n)' }, { label: 'Space', before: a.complexity.space, after: 'O(n)' }, { label: `Runtime at n = ${b.n_used}`, before: `${b.before_ms} ms`, after: `${b.after_ms} ms` }, { label: 'Memory', before: `${b.before_kb} KB`, after: `${b.after_kb} KB` }],
      changes: fib ? ['Added a memo table vector<long long> memo(n + 1, -1)', 'Replaced the two direct recursive calls with a memoized helper'] : ['Removed the nested loop over j (lines 3 to 5)', 'Added an unordered_set<int> seen to record values already visited', 'Replaced the a[i] + a[j] == k test with a complement lookup'],
      tradeoff: fib ? '' : 'Memory rises from constant to linear in n. At the stated bound of n = 100000 this is roughly 400 KB, which is acceptable.',
      verification: 'Compiled successfully and produced identical output to the original on 200 generated inputs, including empty array, single element, all-equal values, and values near INT_MAX.',
      speedup: b.speedup,
    } } };
}

export function trainHistory(req: TrainRequest, upto: number): EpochMetrics[] {
  const total = req.epochs ?? 50;
  const out: EpochMetrics[] = [];
  for (let e = 1; e <= Math.min(upto, total); e++) {
    const t = e / total;
    out.push({ epoch: e, epochs_total: total, train_loss: +(1.9 * Math.exp(-4 * t) + 0.08 + Math.sin(e) * 0.01).toFixed(4), val_loss: +(1.95 * Math.exp(-3.4 * t) + 0.2 + Math.cos(e) * 0.015).toFixed(4), val_accuracy: +(0.9 - 0.8 * Math.exp(-4.5 * t) + Math.sin(e * 1.3) * 0.008).toFixed(4) });
  }
  return out;
}

const STAGES: [number, string][] = [[10, 'parsing'], [20, 'building AST'], [30, 'building CFG'], [45, 'complexity analysis'], [55, 'feature extraction'], [65, 'algorithm prediction'], [75, 'static checks'], [90, 'running tests'], [100, 'optimizing']];

export function mockEpochs(id: number): EpochMetrics[] {
  const r = recs.find((x) => x.job.job_id === id);
  if (!r?.train) return [];
  return trainHistory(r.train, Math.floor(Math.max(0, Date.now() - r.started - 500) / ((r.duration - 500) / (r.train.epochs ?? 50))));
}

function view(r: Rec): Job {
  const j = r.job;
  if (j.status === 'cancelled' || j.status === 'done' || j.status === 'failed') return j;
  const elapsed = Date.now() - r.started;
  if (elapsed < 500) return { ...j, status: 'queued', progress: 0, stage: 'queued' };
  const p = Math.min(100, Math.round((elapsed / r.duration) * 100));
  if (p < 100) return { ...j, status: 'running', progress: p, stage: j.type === 'train' ? `epoch ${mockEpochs(j.job_id).length}` : STAGES.find(([at]) => p <= at)![1] };
  const finished_at = new Date().toISOString();
  if (r.fail) return (r.job = { ...j, status: 'failed', progress: 100, stage: 'failed', finished_at, result: null, error: { code: 'COMPILE_ERROR', message: "line 7: expected ';' before '}'" } });
  if (j.type === 'train') {
    const last = mockEpochs(j.job_id).slice(-1)[0] ?? trainHistory(r.train!, r.train!.epochs ?? 50).slice(-1)[0];
    if (!r.modelAdded) { r.modelAdded = true; models = [...models, { id: models.length + 1, version: `v${new Date().toISOString().slice(0, 10).replace(/-/g, '')}-${String(models.length + 10).padStart(4, '0')}`, cv_accuracy: last.val_accuracy, loss: last.val_loss, epochs: last.epochs_total, dataset_size: 2067, trained_at: finished_at, is_active: false }]; }
    return (r.job = { ...j, status: 'done', progress: 100, stage: 'complete', finished_at, result: null });
  }
  return (r.job = { ...j, status: 'done', progress: 100, stage: 'complete', finished_at, result: j.type === 'optimize' ? optimizeFor(r.code) : analysisFor(r.code) });
}

function create(type: JobType, code: string, problem_id: string | null, duration: number, extra: Partial<Rec> = {}) {
  const job: Job = { job_id: seq++, type, status: 'queued', progress: 0, stage: 'queued', created_at: new Date().toISOString(), finished_at: null, result: null, error: null };
  recs.push({ job, started: Date.now(), duration, code, problem_id, ...extra });
  return { job_id: job.job_id, status: 'queued', created_at: job.created_at, ws_url: `/api/v1/ws/jobs/${job.job_id}` };
}

const row = (r: Rec): JobRow => {
  const v = view(r);
  const res = v.result as AnalysisResult | null;
  return { job_id: v.job_id, type: v.type, status: v.status, problem_id: r.problem_id, algorithm: res?.algorithms?.[0]?.label ?? null, time_complexity: res?.complexity?.time ?? null, created_at: v.created_at };
};

(function seed() {
  const mk = (type: JobType, ago: number, pid: string | null, code: string) => { const x = create(type, code, pid, 1000); const r = recs[recs.length - 1]; r.started = Date.now() - 60000; r.job.created_at = new Date(Date.now() - ago).toISOString(); void x; view(r); };
  mk('analyze', 5 * 3600e3, 'pair_sum', 'for (;;) { for (;;) {} }');
  mk('optimize', 4.9 * 3600e3, 'pair_sum', 'for (;;) { for (;;) {} }');
  mk('analyze', 2 * 86400e3, 'search', 'int mid = 0;');
})();

export async function mockHandle(method: string, fullPath: string, body: any): Promise<unknown> {
  await wait();
  const [path, query = ''] = fullPath.split('?');
  const q = new URLSearchParams(query);
  const bad = (field: string, message: string) => new ApiError(400, 'VALIDATION_FAILED', message, field);

  if (path.endsWith('/health')) { const h: Health = { status: 'ok', db: 'ok', model_loaded: true, model_version: activeVersion(), queue_depth: recs.filter((r) => view(r).status === 'running').length, uptime_s: Math.floor((Date.now() - T0) / 1000) }; return h; }
  if (path.endsWith('/auth/login') || path.endsWith('/auth/signup')) {
    if (!body.username || body.username.length < 3 || body.username.length > 32) throw bad('username', 'username must be 3 to 32 characters');
    if (!body.password || body.password.length < 8) throw bad('password', 'password must be at least 8 characters');
    const user = { id: 1, username: body.username, created_at: '2026-10-01T09:00:00Z' };
    return { token: 'mock-token', expires_at: new Date(Date.now() + 7 * 86400e3).toISOString(), user };
  }
  if (path.endsWith('/auth/me')) return { id: 1, username: 'student', created_at: '2026-10-01T09:00:00Z' };
  if (path.endsWith('/auth/logout')) return undefined;
  if (path.endsWith('/problems')) return { problems: PROBLEMS.map(({ statement: _s, ...p }) => p) };
  const pr = path.match(/\/problems\/([^/]+)$/);
  if (pr) { const p = PROBLEMS.find((x) => x.id === decodeURIComponent(pr[1])); if (!p) throw new ApiError(404, 'NOT_FOUND', 'No such problem'); return p; }
  if (path.endsWith('/execute')) {
    await wait(500);
    if (!body.code?.trim()) throw bad('code', 'code field must not be empty');
    if (/syntax_error/.test(body.code)) return { compiled: false, compile_error: "main.cpp:7:5: error: expected ';' before '}' token", exit_code: -1, stdout: '', stderr: '', execution_time_ms: 0, memory_kb: 0, timed_out: false };
    return { compiled: true, compile_error: null, exit_code: 0, stdout: body.stdin ? `echo: ${body.stdin}` : '(no output)\n', stderr: '', execution_time_ms: 13.4, memory_kb: 12400, timed_out: false };
  }
  if (path.endsWith('/train') && method === 'POST') return create('train', '', null, 500 + (body.epochs ?? 50) * 450, { train: body });
  if (path.endsWith('/models') && method === 'GET') return { models };
  const act = path.match(/\/models\/(\d+)\/activate$/);
  if (act) { models = models.map((m) => ({ ...m, is_active: m.id === Number(act[1]) })); return models.find((m) => m.id === Number(act[1])); }
  if (path.endsWith('/jobs') && method === 'POST') {
    if (!body.code?.trim()) throw bad('code', 'code field must not be empty');
    if (new Blob([body.code]).size > 65536) throw new ApiError(413, 'CODE_TOO_LARGE', 'Source exceeds 64 KB');
    if (!body.problem_id && !body.problem) throw bad('problem_id', 'provide problem_id or problem');
    return create(body.type, body.code, body.problem_id ?? null, body.type === 'optimize' ? 6500 : 4500, { fail: /syntax_error/.test(body.code) });
  }
  if (path.endsWith('/jobs')) {
    const limit = Math.min(100, Number(q.get('limit') ?? 20)), offset = Number(q.get('offset') ?? 0);
    const all = [...recs].reverse().map(row).filter((r) => (!q.get('type') || r.type === q.get('type')) && (!q.get('status') || r.status === q.get('status')));
    return { jobs: all.slice(offset, offset + limit), total: all.length, limit, offset };
  }
  const one = path.match(/\/jobs\/(\d+)$/);
  if (one) {
    const r = recs.find((x) => x.job.job_id === Number(one[1]));
    if (!r) throw new ApiError(404, 'NOT_FOUND', 'No such job');
    if (method === 'DELETE') { const v = view(r); if (v.status === 'done' || v.status === 'failed' || v.status === 'cancelled') throw new ApiError(409, 'CONFLICT', 'Job already finished'); r.job = { ...v, status: 'cancelled', stage: 'cancelled', finished_at: new Date().toISOString() }; return undefined; }
    return view(r);
  }
  throw new ApiError(404, 'NOT_FOUND', `Mock: no route for ${method} ${path}`);
}

export function mockSubscribe(jobId: number, onEvent: (e: WsEvent) => void, onState: (c: boolean) => void): () => void {
  onState(true);
  let sent = 0;
  const t = window.setInterval(() => {
    const r = recs.find((x) => x.job.job_id === jobId);
    if (!r) return;
    const j = view(r);
    if (j.type !== 'train') onEvent({ event: 'progress', job_id: jobId, percent: j.progress, stage: j.stage });
    for (const e of mockEpochs(jobId).slice(sent)) { onEvent({ event: 'epoch', job_id: jobId, ...e }); sent++; }
    if (j.status === 'failed') { onEvent({ event: 'failed', job_id: jobId, error: j.error! }); window.clearInterval(t); }
    else if (j.status === 'done' || j.status === 'cancelled') { onEvent({ event: 'done', job_id: jobId }); window.clearInterval(t); }
  }, 350);
  return () => window.clearInterval(t);
}
