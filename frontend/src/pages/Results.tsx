import { ArrowLeft, Sparkles, XCircle } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, Navigate, useNavigate, useParams } from 'react-router-dom';
import { api } from '../api/client';
import AnalysisView from '../components/analysis/AnalysisView';
import PipelineProgress from '../components/analysis/PipelineProgress';
import { Badge, Button, ErrorBox, Loading, PageHeader } from '../components/common/ui';
import { useJob } from '../hooks/useJob';
import { JOB_ERROR_TITLE, errorMessage } from '../lib/errors';
import { isTerminal, relTime, statusTone } from '../lib/format';
import { loadSubmission } from '../lib/jobStore';
import { submitJob } from '../lib/submit';
import type { AnalysisResult } from '../types/api';

export default function Results() {
  const id = Number(useParams().id);
  const nav = useNavigate();
  const { job, error, live, refresh } = useJob(id);
  const sub = useMemo(() => loadSubmission(id), [id]);
  const [starting, setStarting] = useState(false);
  const [actErr, setActErr] = useState('');

  if (error && !job) return <ErrorBox title="Could not load this analysis" action={<Button size="sm" variant="secondary" onClick={() => void refresh()}>Try again</Button>}>{error}</ErrorBox>;
  if (!job) return <Loading label="Loading analysis" />;
  if (job.type === 'optimize') return <Navigate to={`/optimize/${job.job_id}`} replace />;
  if (job.type === 'train') return <Navigate to="/training" replace />;

  const r = job.result as AnalysisResult | null;
  const running = !isTerminal(job.status);
  const optimize = async () => {
    if (!sub) return;
    setActErr(''); setStarting(true);
    try { nav(`/optimize/${await submitJob('optimize', sub)}`); } catch (e) { setActErr(errorMessage(e)); setStarting(false); }
  };
  const cancel = async () => { try { await api.jobs.cancel(job.job_id); } catch { /* already finished */ } await refresh(); };
  const edit = () => sub && nav('/analyze', { state: { code: sub.code } });

  return (
    <>
      <Link to="/analyze" className="mb-4 inline-flex items-center gap-1.5 text-sm text-mute hover:text-ink"><ArrowLeft className="h-4 w-4" />Back to editor</Link>
      <PageHeader title={`Analysis #${job.job_id}`} description={`Started ${relTime(job.created_at)}${sub?.problem_id ? ` for problem ${sub.problem_id}` : sub ? ' with a custom problem' : ''}.`}
        actions={<>
          <Badge tone={statusTone[job.status]} className="!px-3 !py-1.5 !text-sm">{job.status}</Badge>
          {running && <Button variant="danger" icon={<XCircle className="h-4 w-4" />} onClick={() => void cancel()}>Cancel</Button>}
          {job.status === 'done' && r?.optimization_available && sub && <Button icon={<Sparkles className="h-4 w-4" />} loading={starting} onClick={() => void optimize()}>Optimize this code</Button>}
          {job.status !== 'queued' && !running && sub && <Button variant="secondary" onClick={edit}>Edit and re-run</Button>}
        </>} />
      {actErr && <div className="mb-4"><ErrorBox title="Could not start the optimization">{actErr}</ErrorBox></div>}

      {running && <PipelineProgress percent={live.percent || job.progress} stage={live.stage || job.stage} status={job.status} />}
      {job.status === 'failed' && (
        <ErrorBox title={JOB_ERROR_TITLE[job.error?.code ?? ''] ?? 'The analysis failed'}>
          <pre className="whitespace-pre-wrap font-mono text-xs">{job.error?.message ?? 'The server reported an error without details.'}</pre>
        </ErrorBox>
      )}
      {job.status === 'cancelled' && <ErrorBox title="This analysis was cancelled" />}
      {job.status === 'done' && r && <AnalysisView r={r} code={sub?.code ?? null} />}
      {job.status === 'done' && !r && <ErrorBox title="The job finished without a result" />}
    </>
  );
}
