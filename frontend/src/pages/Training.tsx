import { Play, XCircle } from 'lucide-react';
import { useCallback, useEffect, useState } from 'react';
import { api } from '../api/client';
import LiveChart from '../components/charts/LiveChart';
import { Badge, Button, Card, EmptyState, ErrorBox, Loading, PageHeader, ProgressBar, Toggle } from '../components/common/ui';
import { useJob } from '../hooks/useJob';
import { JOB_ERROR_TITLE, errorMessage } from '../lib/errors';
import { fmtN, fmtPct, isTerminal, relTime } from '../lib/format';
import type { EpochMetrics, ModelInfo } from '../types/api';
import ActiveModelCard from '../components/training/ActiveModelCard';
const KEY = 'nc.trainJob';
const DEFAULTS = { epochs: 50, batch_size: 32, learning_rate: 0.001 };

export default function Training() {
  const [useServerDefaults, setUseServerDefaults] = useState(true);
  const [cfg, setCfg] = useState(DEFAULTS);
  const [jobId, setJobId] = useState<number | undefined>(() => Number(localStorage.getItem(KEY)) || undefined);
  const [starting, setStarting] = useState(false);
  const [err, setErr] = useState('');
  const [models, setModels] = useState<ModelInfo[] | null>(null);
  const [modelErr, setModelErr] = useState('');
  const { job, live } = useJob(jobId);

  const loadModels = useCallback(() => { api.models.list().then((m) => { setModels(m); setModelErr(''); }).catch((e) => setModelErr(errorMessage(e))); }, []);
  useEffect(loadModels, [loadModels]);
  useEffect(() => { if (job?.status === 'done') loadModels(); }, [job?.status, loadModels]);

  const set = <K extends keyof typeof DEFAULTS>(k: K, v: (typeof DEFAULTS)[K]) => setCfg({ ...cfg, [k]: v });
  const start = async () => {
    setErr(''); setStarting(true);
    try {
      const { job_id } = await api.train(useServerDefaults ? {} : cfg);
      localStorage.setItem(KEY, String(job_id)); setJobId(job_id);
    } catch (e) { setErr(errorMessage(e)); } finally { setStarting(false); }
  };
  const activate = async (id: number) => { try { await api.models.activate(id); setModelErr(''); loadModels(); } catch (e) { setModelErr(errorMessage(e)); } };

  const running = !!job && !isTerminal(job.status);
  const data: EpochMetrics[] = live.epochs;
  const last = data[data.length - 1];

  return (
    <>
      <PageHeader title="Model training" description="Train the algorithm classifier and watch the loss per epoch. A newly trained model stays inactive until you activate it." />
      <div className="grid gap-6 xl:grid-cols-[360px_minmax(0,1fr)]">
        <Card title="Training settings" subtitle="Every field is optional on the server.">
          <div className="space-y-5">
            <Toggle label="Use server defaults (config.json)" checked={useServerDefaults} onChange={setUseServerDefaults} />
            <fieldset disabled={useServerDefaults} className="space-y-5 disabled:opacity-50">
              <div><label className="label" htmlFor="ep">Epochs</label><input id="ep" type="number" min={1} max={500} className="field" value={cfg.epochs} onChange={(e) => set('epochs', Math.max(1, Number(e.target.value) || 1))} />
                <p className="hint">Maximum number of passes over the training set.</p></div>
              <div><label className="label" htmlFor="bs">Batch size</label>
                <select id="bs" className="field" value={cfg.batch_size} onChange={(e) => set('batch_size', Number(e.target.value))}>{[16, 32, 64, 128].map((v) => <option key={v}>{v}</option>)}</select></div>
              <div><label className="label" htmlFor="lr">Learning rate</label>
                <select id="lr" className="field" value={cfg.learning_rate} onChange={(e) => set('learning_rate', Number(e.target.value))}>{[0.0001, 0.0005, 0.001, 0.003, 0.01].map((v) => <option key={v} value={v}>{v}</option>)}</select></div>
            </fieldset>
            {err && <ErrorBox title="Could not start training">{err}</ErrorBox>}
            <Button size="lg" className="w-full" icon={<Play className="h-5 w-5" />} loading={starting} disabled={running} onClick={() => void start()}>{running ? 'Training in progress' : 'Start training'}</Button>
            {running && <Button variant="danger" className="w-full" icon={<XCircle className="h-4 w-4" />} onClick={() => void api.jobs.cancel(job!.job_id).catch(() => undefined)}>Cancel run</Button>}
          </div>
        </Card>

        <div className="space-y-6">
          {running && (
            <Card>
              <div className="mb-2 flex justify-between text-sm"><span>{last ? `Epoch ${last.epoch} of ${last.epochs_total}` : live.stage || job!.stage || 'Preparing the dataset'}</span><span className="tabular-nums text-mute">{last ? Math.round((last.epoch / last.epochs_total) * 100) : job!.progress}%</span></div>
              <ProgressBar percent={last ? (last.epoch / last.epochs_total) * 100 : job!.progress} />
            </Card>
          )}
          {job?.status === 'failed' && <ErrorBox title={JOB_ERROR_TITLE[job.error?.code ?? ''] ?? 'Training failed'}>{job.error?.message}</ErrorBox>}
          <Card title="Loss and accuracy" subtitle="Per epoch, from the live progress stream">
            {data.length === 0 ? <EmptyState title="No epoch data">Start a run to see its curves. Epochs are streamed live and are not stored, so a finished run is not shown after a page reload.</EmptyState> : (
              <>
                <div className="mb-4 grid grid-cols-3 gap-3 text-sm">
                  {[['Epoch', `${last.epoch} / ${last.epochs_total}`], ['Validation loss', last.val_loss.toFixed(3)], ['Validation accuracy', fmtPct(last.val_accuracy, 1)]].map(([l, v]) => (
                    <div key={l} className="rounded-lg bg-raised px-4 py-3"><p className="text-mute">{l}</p><p className="font-display text-2xl font-bold tabular-nums">{v}</p></div>
                  ))}
                </div>
                <LiveChart data={data} />
              </>
            )}
          </Card>

          <ActiveModelCard models={models} />

          <Card title="Saved models" subtitle="Activating a model changes the predictions for new analyses only." pad={false}>
            {modelErr && <div className="p-5"><ErrorBox title="Model request failed">{modelErr}</ErrorBox></div>}
            {!models && !modelErr ? <Loading label="Loading models" /> : models && models.length === 0 ? <div className="p-5"><EmptyState title="No models yet">Train one to get started.</EmptyState></div> : models && (
              <div className="overflow-x-auto"><table className="w-full text-left text-sm">
                <thead><tr className="border-b border-line text-mute">{['Version', 'CV accuracy', 'Loss', 'Epochs', 'Samples', 'Trained', ''].map((h) => <th key={h} className="px-5 py-3 font-medium">{h}</th>)}</tr></thead>
                <tbody>{[...models].reverse().map((m) => {
                  const acc = m.cv_accuracy ?? m.accuracy;
                  return (
                    <tr key={m.id} className="border-b border-line last:border-0">
                      <td className="px-5 py-3 font-mono">{m.version}</td><td className="px-5 py-3 tabular-nums">{acc != null ? fmtPct(acc, 1) : '-'}</td><td className="px-5 py-3 tabular-nums">{m.loss != null ? m.loss.toFixed(3) : '-'}</td>
                      <td className="px-5 py-3 tabular-nums">{m.epochs}</td><td className="px-5 py-3 tabular-nums">{fmtN(m.dataset_size)}</td><td className="px-5 py-3 text-mute">{relTime(m.trained_at)}</td>
                      <td className="px-5 py-3 text-right">{m.is_active ? <Badge tone="ok">Active</Badge> : <Button size="sm" variant="secondary" onClick={() => void activate(m.id)}>Activate</Button>}</td>
                    </tr>);
                })}</tbody>
              </table></div>
            )}
          </Card>
        </div>
      </div>
    </>
  );
}