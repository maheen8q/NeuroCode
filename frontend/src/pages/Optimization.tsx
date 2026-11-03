import { ArrowLeft } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import AnalysisView from '../components/analysis/AnalysisView';
import PipelineProgress from '../components/analysis/PipelineProgress';
import { Badge, Button, Card, ErrorBox, Loading, PageHeader, Tabs } from '../components/common/ui';
import DiffViewer from '../components/editor/DiffViewer';
import OptimizedCodePanel from '../components/editor/OptimizedCodePanel';
import ChangeList from '../components/optimization/ChangeList';
import ComparisonTable from '../components/optimization/ComparisonTable';
import SummaryCard from '../components/optimization/SummaryCard';
import VerificationBadge from '../components/optimization/VerificationBadge';
import BenchmarkChart from '../components/visualization/BenchmarkChart';
import { useJob } from '../hooks/useJob';
import { JOB_ERROR_TITLE } from '../lib/errors';
import { isTerminal, statusTone } from '../lib/format';
import { loadSubmission } from '../lib/jobStore';
import type { OptimizeResult } from '../types/api';

export default function Optimization() {
  const id = Number(useParams().id);
  const nav = useNavigate();
  const { job, error, live, refresh } = useJob(id);
  const sub = useMemo(() => loadSubmission(id), [id]);
  const [tab, setTab] = useState<'diff' | 'code'>('diff');
  const r = job?.result as OptimizeResult | null | undefined;

  if (error && !job) return <ErrorBox title="Could not load this optimization" action={<Button size="sm" variant="secondary" onClick={() => void refresh()}>Try again</Button>}>{error}</ErrorBox>;
  if (!job) return <Loading label="Loading optimization" />;
  const running = !isTerminal(job.status);
  const o = r?.optimization;
  // Spec rendering rule: never show generated code unless it was verified.
  const verified = !!o && o.verified && !!o.optimized_code && !!o.summary;
  const edit = (code: string) => nav('/analyze', { state: { code } });

  return (
    <>
      <Link to="/history" className="mb-4 inline-flex items-center gap-1.5 text-sm text-mute hover:text-ink"><ArrowLeft className="h-4 w-4" />History</Link>
      <PageHeader title={`Optimization #${job.job_id}`} description="The optimized version is shown only if it matched the original on every generated test."
        actions={<Badge tone={statusTone[job.status]} className="!px-3 !py-1.5 !text-sm">{job.status}</Badge>} />

      {running && <PipelineProgress percent={live.percent || job.progress} stage={live.stage || job.stage} status={job.status} />}
      {job.status === 'failed' && (
        <ErrorBox title={JOB_ERROR_TITLE[job.error?.code ?? ''] ?? 'The optimization failed'}>
          <pre className="whitespace-pre-wrap font-mono text-xs">{job.error?.message ?? 'The server reported an error without details.'}</pre>
        </ErrorBox>
      )}
      {job.status === 'cancelled' && <ErrorBox title="This optimization was cancelled" />}

      {job.status === 'done' && r && (
        <div className="space-y-8">
          {verified && o.summary && o.optimized_code ? (
            <>
              <SummaryCard s={o.summary} bench={o.benchmark}
                verification={<VerificationBadge verified testsRun={o.verification?.tests_run} mismatches={o.verification?.mismatches} />} />
              <div className="grid gap-6 lg:grid-cols-2"><ComparisonTable rows={o.summary.comparison} /><ChangeList changes={o.summary.changes} /></div>
              <Card title="Code" action={<div className="flex gap-1 rounded-lg border border-line bg-raised p-0.5">{([['diff', 'Diff'], ['code', 'Optimized code']] as const).map(([k, l]) => <button key={k} onClick={() => setTab(k)} aria-pressed={tab === k} className={`rounded-md px-3 py-1 text-sm ${tab === k ? 'bg-panel font-medium' : 'text-mute'}`}>{l}</button>)}</div>}>
                {tab === 'diff'
                  ? (sub ? <DiffViewer original={sub.code} modified={o.optimized_code} /> : <p className="text-mute">The original source is not stored in this browser, so there is nothing to diff against. Open the Optimized code tab instead.</p>)
                  : <OptimizedCodePanel code={o.optimized_code} onUse={() => edit(o.optimized_code!)} />}
              </Card>
              {o.benchmark && o.summary.speedup > 0 && <Card title="Measured runtime"><BenchmarkChart bench={o.benchmark} /></Card>}
            </>
          ) : (
            <Card title="No verified optimization found" action={<VerificationBadge verified={false} />}>
              <p className="max-w-2xl text-mute">Either no transformation applies to this code, or the generated candidate did not match the original on the tests. Unverified code is never shown. The analysis of your original code is below.</p>
              {sub && <div className="mt-4"><Button variant="secondary" onClick={() => edit(sub.code)}>Edit code</Button></div>}
            </Card>
          )}
          <section>
            <h2 className="mb-4 text-xl font-bold">Analysis of your original code</h2>
            <AnalysisView r={r} code={sub?.code ?? null} />
          </section>
        </div>
      )}
      {job.status === 'done' && !r && <ErrorBox title="The job finished without a result" />}
    </>
  );
}
