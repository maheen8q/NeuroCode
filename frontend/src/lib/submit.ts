import { api } from '../api/client';
import type { CreateJobRequest } from '../types/api';
import { saveSubmission, type Submission } from './jobStore';

export const MAX_CODE_BYTES = 64 * 1024; // spec: CODE_TOO_LARGE above 64 KB
export const codeBytes = (code: string) => new Blob([code]).size;

/** Creates an analyze or optimize job and remembers what was submitted. Returns the job id. */
export async function submitJob(type: 'analyze' | 'optimize', s: Submission): Promise<number> {
  const body: CreateJobRequest = { type, language: 'cpp', code: s.code, ...(s.problem_id ? { problem_id: s.problem_id } : { problem: s.problem! }) };
  const { job_id } = await api.jobs.create(body);
  saveSubmission(job_id, s);
  return job_id;
}
