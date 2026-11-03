import { useEffect, useState } from 'react';
import { subscribeJob } from '../api/socket';
import type { EpochMetrics, JobError } from '../types/api';

export interface LiveState {
  jobId?: number; percent: number; stage: string; connected: boolean;
  epochs: EpochMetrics[]; done: boolean; error: JobError | null;
}
const EMPTY: LiveState = { percent: 0, stage: '', connected: false, epochs: [], done: false, error: null };

/** Keeps its last state after the job ends, so a finished training chart stays on screen. */
export function useProgressSocket(jobId?: number): LiveState {
  const [s, setS] = useState<LiveState>(EMPTY);
  useEffect(() => {
    if (jobId === undefined) return;
    setS({ ...EMPTY, jobId });
    return subscribeJob(
      jobId,
      (e) => setS((p) => {
        switch (e.event) {
          case 'progress': return { ...p, percent: e.percent, stage: e.stage };
          case 'epoch': return { ...p, epochs: [...p.epochs, { epoch: e.epoch, epochs_total: e.epochs_total, train_loss: e.train_loss, val_loss: e.val_loss, val_accuracy: e.val_accuracy }] };
          case 'done': return { ...p, done: true };
          case 'failed': return { ...p, done: true, error: e.error };
        }
      }),
      (connected) => setS((p) => ({ ...p, connected })),
    );
  }, [jobId]);
  return s;
}
