import type { ProblemSpec } from '../types/api';

/**
 * GET /jobs/{id} does not return the submitted source or problem, so the browser keeps its own copy.
 * Needed for the code tab, the diff view, "Edit and re-run" and "Optimize this code".
 */
export interface Submission { code: string; problem_id: string | null; problem: ProblemSpec | null }
const KEY = (id: number) => `nc.job.${id}`;
const INDEX = 'nc.jobs.index';
const KEEP = 60;

export function saveSubmission(id: number, s: Submission) {
  try {
    localStorage.setItem(KEY(id), JSON.stringify(s));
    const idx: number[] = JSON.parse(localStorage.getItem(INDEX) || '[]');
    idx.push(id);
    while (idx.length > KEEP) localStorage.removeItem(KEY(idx.shift()!));
    localStorage.setItem(INDEX, JSON.stringify(idx));
  } catch { /* storage full or disabled: the UI falls back gracefully */ }
}

export function loadSubmission(id: number): Submission | null {
  try { return JSON.parse(localStorage.getItem(KEY(id)) || 'null') as Submission | null; } catch { return null; }
}
