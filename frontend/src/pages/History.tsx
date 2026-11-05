import { ChevronLeft, ChevronRight, RefreshCw } from 'lucide-react';
import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { api } from '../api/client';
import { Badge, Button, EmptyState, ErrorBox, Loading, PageHeader, Segmented } from '../components/common/ui';
import { errorMessage } from '../lib/errors';
import { jobHref, relTime, statusTone, typeLabel } from '../lib/format';
import { humanize } from '../lib/labels';
import type { JobPage, JobStatus, JobType } from '../types/api';

const LIMIT = 20;

export default function History() {
  const [data, setData] = useState<JobPage | null>(null);
  const [err, setErr] = useState('');
  const [type, setType] = useState<'all' | JobType>('all');
  const [status, setStatus] = useState<'all' | JobStatus>('all');
  const [offset, setOffset] = useState(0);

  const load = useCallback(() => {
    setErr('');
    api.jobs.list({ limit: LIMIT, offset, type: type === 'all' ? undefined : type, status: status === 'all' ? undefined : status })
      .then(setData).catch((e) => setErr(errorMessage(e)));
  }, [offset, type, status]);
  useEffect(load, [load]);
  useEffect(() => setOffset(0), [type, status]);

  const from = data && data.total > 0 ? data.offset + 1 : 0;
  const to = data ? Math.min(data.offset + data.limit, data.total) : 0;

  return (
    <>
      <PageHeader title="History" description="Your analyses, optimizations and training runs."
        actions={<Button variant="secondary" icon={<RefreshCw className="h-4 w-4" />} onClick={load}>Refresh</Button>} />
      <div className="mb-5 flex flex-wrap items-center gap-3">
        <Segmented value={type} onChange={setType} label="Filter by type" options={[{ value: 'all', label: 'All' }, { value: 'analyze', label: 'Analysis' }, { value: 'optimize', label: 'Optimization' }, { value: 'train', label: 'Training' }]} />
        <select className="field !w-40" aria-label="Filter by status" value={status} onChange={(e) => setStatus(e.target.value as 'all' | JobStatus)}>
          {['all', 'queued', 'running', 'done', 'failed', 'cancelled'].map((s) => <option key={s} value={s}>{s === 'all' ? 'Any status' : s}</option>)}
        </select>
      </div>
      {err && <ErrorBox title="Could not load your history" action={<Button size="sm" variant="secondary" onClick={load}>Try again</Button>}>{err}</ErrorBox>}
      {!data && !err && <Loading label="Loading history" />}
      {data && data.total === 0 && <EmptyState title={type === 'all' && status === 'all' ? 'No jobs yet' : 'No jobs match these filters'} action={type === 'all' && status === 'all' ? <Link to="/analyze"><Button>Analyze code</Button></Link> : undefined}>Jobs appear here as soon as you run one.</EmptyState>}
      {data && data.total > 0 && (
        <>
          <div className="overflow-x-auto rounded-xl border border-line bg-panel">
            <table className="w-full text-left text-sm">
              <thead><tr className="border-b border-line text-mute">{['Job', 'Type', 'Problem', 'Detected algorithm', 'Time', 'Status', 'Started'].map((h) => <th key={h} className="px-5 py-3 font-medium">{h}</th>)}</tr></thead>
              <tbody>
                {data.jobs.map((j) => (
                  <tr key={j.id} className="border-b border-line last:border-0 hover:bg-raised/60">
                    <td className="px-5 py-3"><Link to={jobHref(j)} className="font-medium text-accent hover:underline">#{j.id}</Link></td>
                    <td className="px-5 py-3">{typeLabel[j.type]}</td>
                    <td className="px-5 py-3 font-mono text-mute">{j.type === 'train' ? '-' : j.problem_id ?? 'custom'}</td>
                    <td className="px-5 py-3">{j.type === 'train' ? '-' : humanize(j.algorithm)}</td>
                    <td className="px-5 py-3 font-mono">{j.time_complexity ?? '-'}</td>
                    <td className="px-5 py-3"><Badge tone={statusTone[j.status]}>{j.status}</Badge></td>
                    <td className="px-5 py-3 text-mute">{relTime(j.created_at)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <div className="mt-4 flex items-center justify-between text-sm text-mute">
            <span>Showing {from} to {to} of {data.total}</span>
            <div className="flex gap-2">
              <Button size="sm" variant="secondary" icon={<ChevronLeft className="h-4 w-4" />} disabled={offset === 0} onClick={() => setOffset(Math.max(0, offset - LIMIT))}>Previous</Button>
              <Button size="sm" variant="secondary" disabled={to >= data.total} onClick={() => setOffset(offset + LIMIT)}>Next<ChevronRight className="h-4 w-4" /></Button>
            </div>
          </div>
        </>
      )}
    </>
  );
}
