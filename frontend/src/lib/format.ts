import type { JobStatus, JobType } from '../types/api';

export const isTerminal = (s: JobStatus) => s === 'done' || s === 'failed' || s === 'cancelled';
export const fmtMs = (ms: number) => (ms >= 1000 ? `${(ms / 1000).toFixed(2)} s` : `${ms.toFixed(ms < 10 ? 2 : 1)} ms`);
export const fmtKb = (kb: number) => (kb >= 1024 ? `${(kb / 1024).toFixed(1)} MB` : `${kb} KB`);
export const fmtPct = (p: number, digits = 0) => `${(p * 100).toFixed(digits)}%`;
export const fmtN = (n: number) => n.toLocaleString('en-US');
export const fmtSpeedup = (x: number) => `${x >= 100 ? Math.round(x).toLocaleString('en-US') : x.toFixed(1)}x`;

export function relTime(iso: string | null): string {
  if (!iso) return '-';
  const s = Math.max(0, (Date.now() - new Date(iso).getTime()) / 1000);
  if (s < 60) return 'just now';
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`;
  return new Date(iso).toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
}

export type Tone = 'neutral' | 'ok' | 'warn' | 'bad' | 'accent';
export const statusTone: Record<JobStatus, Tone> = { queued: 'neutral', running: 'accent', done: 'ok', failed: 'bad', cancelled: 'warn' };
export const typeLabel: Record<JobType, string> = { analyze: 'Analysis', optimize: 'Optimization', train: 'Training' };
export const jobHref = (j: { job_id: number; type: JobType }) =>
  j.type === 'optimize' ? `/optimize/${j.job_id}` : j.type === 'train' ? '/training' : `/jobs/${j.job_id}`;
