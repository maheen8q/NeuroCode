// Hand-mirrored from docs/api-spec.md (v1.0). If the spec changes, change this file the same day.

export type JobType = 'analyze' | 'optimize' | 'train';
export type JobStatus = 'queued' | 'running' | 'done' | 'failed' | 'cancelled';
export type Severity = 'high' | 'medium' | 'low';
export type FindingType =
  | 'unreachable' | 'missing_return' | 'overflow_risk' | 'off_by_one_suspect' | 'unused_variable'
  | 'infinite_loop_suspect' | 'uninitialized_use' | 'inefficient_container';
export type AlgorithmLabel =
  | 'binary_search' | 'brute_force_pair_search' | 'dp' | 'graph_traversal' | 'greedy_scan' | 'hashing'
  | 'heap_based' | 'linear_search' | 'naive_recursion' | 'sliding_window' | 'sorting_based' | 'two_pointer' | 'unknown';

/** Every failure: { error: { code, message, field? } } */
export interface ErrorEnvelope { error: { code: string; message: string; field?: string } }
export interface JobError { code: string; message: string }

export interface User { id: number; username: string; created_at: string }
export interface AuthResponse { token: string; expires_at: string; user: User }

export interface Health {
  status: 'ok' | 'degraded'; db: string; model_loaded: boolean; model_version?: string; queue_depth: number; uptime_s: number;
}

export interface ProblemSpec { input_type: string; output_type: string; operation: string; max_n: number }
export interface Problem extends ProblemSpec { id: string; title: string; optimal_time: string; statement?: string; category?: string; difficulty?: string }

export interface AlgorithmGuess { label: string; probability: number }
export interface Complexity { time: string; space: string; derivation: string[] }
export interface Finding { line: number; severity: Severity; type: string; message: string }
export interface Counterexample { input: string; expected_output: string; actual_output: string }
export interface ExecutionSummary { compiled: boolean; compile_error: string | null; tests_run: number; tests_passed: number }

// The spec only shows { type, name, children } and { nodes, edges }. Extra fields are optional on purpose.
export interface AstNode { type: string; name?: string; label?: string; line?: number; children?: AstNode[] }
export interface CfgNode { id: string | number; label?: string; name?: string; kind?: string; type?: string; line?: number }
export interface CfgEdge { from?: string | number; to?: string | number; source?: string | number; target?: string | number; label?: string }
export interface CfgGraph { nodes: CfgNode[]; edges: CfgEdge[] }

export interface AnalysisResult {
  algorithms: AlgorithmGuess[];
  low_confidence: boolean;
  model_version: string;
  complexity: Complexity;
  findings: Finding[];
  counterexamples: Counterexample[];
  execution: ExecutionSummary;
  ast: AstNode;
  cfg: CfgGraph;
  optimization_available: boolean;
}

export interface SummaryRow { label: string; before: string; after: string }
export interface OptimizationSummary {
  headline: string; explanation: string; comparison: SummaryRow[]; changes: string[];
  tradeoff: string; verification: string; speedup: number; // 0 means benchmarking failed: hide it
}
export interface Benchmark { n_used: number; before_ms: number; after_ms: number; speedup: number; before_kb: number; after_kb: number }
export interface Optimization {
  original_algorithm: string; original_complexity: string;
  recommended_algorithm: string; recommended_complexity: string;
  optimized_code: string | null;       // null when verified is false
  summary: OptimizationSummary | null; // null when verified is false
  verified: boolean;
  verification: { tests_run: number; mismatches: number } | null;
  benchmark: Benchmark | null;
}
export interface OptimizeResult extends AnalysisResult { optimization: Optimization }

export interface Job {
  job_id: number; type: JobType; status: JobStatus; progress: number; stage: string;
  created_at: string; finished_at: string | null;
  result: unknown;           // AnalysisResult | OptimizeResult | null. The spec does not define a train result.
  error?: JobError | null;   // present when status is "failed"
}
export interface JobRow {
  job_id: number; type: JobType; status: JobStatus; problem_id: string | null;
  algorithm: string | null; time_complexity: string | null; created_at: string;
}
export interface JobPage { jobs: JobRow[]; total: number; limit: number; offset: number }

export interface CreateJobRequest {
  type: 'analyze' | 'optimize'; language: 'cpp'; code: string;
  problem_id?: string; problem?: ProblemSpec; // exactly one of the two
}
export interface CreateJobResponse { job_id: number; status: JobStatus; created_at: string; ws_url: string }

export interface ExecuteRequest { language: 'cpp'; code: string; stdin: string; timeout_ms?: number }
export interface ExecuteResponse {
  compiled: boolean; compile_error: string | null; exit_code: number; stdout: string; stderr: string;
  execution_time_ms: number; memory_kb: number; timed_out: boolean;
}

export interface ModelInfo {
  id: number; version: string; cv_accuracy?: number | null; accuracy?: number | null; loss: number | null;
  epochs: number; dataset_size: number; trained_at: string; is_active: boolean;
}
export interface TrainRequest { epochs?: number; batch_size?: number; learning_rate?: number; dataset?: string }
export interface EpochMetrics { epoch: number; epochs_total: number; train_loss: number; val_loss: number; val_accuracy: number }

// WebSocket /api/v1/ws/jobs/{id}. The socket never carries results: on "done", call GET /jobs/{id}.
export type WsEvent =
  | { event: 'progress'; job_id: number; percent: number; stage: string }
  | ({ event: 'epoch'; job_id: number } & EpochMetrics)
  | { event: 'done'; job_id: number }
  | { event: 'failed'; job_id: number; error: JobError };
