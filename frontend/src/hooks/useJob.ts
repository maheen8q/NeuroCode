import { useCallback, useEffect, useState } from 'react';
import { api } from '../api/client';
import { errorMessage } from '../lib/errors';
import { isTerminal } from '../lib/format';
import type { Job } from '../types/api';
import { useProgressSocket, type LiveState } from './useProgressSocket';

const NO_LIVE: LiveState = { percent: 0, stage: '', connected: false, epochs: [], done: false, error: null };

/**
 * Loads a job, shows live progress over the WebSocket and polls GET /jobs/{id} every 2 s
 * until it finishes. The spec says never to rely on the socket alone.
 */
export function useJob(id?: number) {
  const [job, setJob] = useState<Job | null>(null);
  const [error, setError] = useState<string | null>(null);
  const active = job && !isTerminal(job.status) ? job.job_id : undefined;
  const socket = useProgressSocket(active ?? (job?.type === 'train' ? job.job_id : undefined));
  const live = job && socket.jobId === job.job_id ? socket : NO_LIVE;

  const refresh = useCallback(async () => {
    if (id === undefined || Number.isNaN(id)) return;
    try { setJob(await api.jobs.get(id)); setError(null); } catch (e) { setError(errorMessage(e)); }
  }, [id]);

  useEffect(() => { setJob(null); setError(null); void refresh(); }, [refresh]);
  useEffect(() => {
    if (!job || isTerminal(job.status)) return;
    const t = window.setInterval(refresh, 2000);
    return () => window.clearInterval(t);
  }, [job, refresh]);
  useEffect(() => { if (live.done) void refresh(); }, [live.done, refresh]);

  return { job, error, live, refresh };
}
