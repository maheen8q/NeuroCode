import clsx from 'clsx';
import { Search } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api/client';
import { errorMessage } from '../../lib/errors';
import { fmtN } from '../../lib/format';
import type { Problem } from '../../types/api';
import { Badge, ErrorBox, Loading } from '../common/ui';

const DIFFICULTY_TONE: Record<string, 'ok' | 'warn' | 'bad'> = { Easy: 'ok', Medium: 'warn', Hard: 'bad' };

export default function ProblemPicker({ selectedId, onSelect, initialId }: { selectedId: string; onSelect: (p: Problem) => void; initialId?: string }) {
  const [items, setItems] = useState<Problem[] | null>(null);
  const [err, setErr] = useState('');
  const [q, setQ] = useState('');
  const [category, setCategory] = useState<string>('All');
  const [statement, setStatement] = useState('');
  useEffect(() => { api.problems.list().then(setItems).catch((e) => setErr(errorMessage(e))); }, []);
  useEffect(() => { if (items && initialId && !selectedId) { const p = items.find((x) => x.id === initialId); if (p) onSelect(p); } }, [items, initialId, selectedId, onSelect]);
  useEffect(() => {
    setStatement('');
    if (selectedId) api.problems.get(selectedId).then((p) => setStatement(p.statement ?? '')).catch(() => setStatement(''));
  }, [selectedId]);

  const categories = useMemo(() => {
    if (!items) return [];
    return ['All', ...Array.from(new Set(items.map((p) => p.category).filter((c): c is string => !!c)))];
  }, [items]);

  if (err) return <ErrorBox title="Could not load the problem catalog">{err}</ErrorBox>;
  if (!items) return <Loading label="Loading problems" />;

  const shown = items.filter((p) => {
    const matchesQuery = (p.title + p.operation).toLowerCase().includes(q.toLowerCase());
    const matchesCategory = category === 'All' || p.category === category;
    return matchesQuery && matchesCategory;
  });

  return (
    <div>
      <div className="relative mb-3">
        <Search className="pointer-events-none absolute left-3 top-2.5 h-4 w-4 text-mute" />
        <input className="field !pl-9" placeholder="Search problems" value={q} onChange={(e) => setQ(e.target.value)} aria-label="Search problems" />
      </div>

      {categories.length > 2 && (
        <div className="mb-3 flex flex-wrap gap-1.5" role="group" aria-label="Filter by category">
          {categories.map((c) => (
            <button
              key={c}
              onClick={() => setCategory(c)}
              aria-pressed={category === c}
              className={clsx(
                'rounded-full border px-3 py-1 text-xs font-medium transition-colors',
                category === c ? 'border-accent bg-accent/15 text-accent' : 'border-line text-mute hover:border-accent/50'
              )}
            >
              {c}
            </button>
          ))}
        </div>
      )}

      <div className="space-y-2">
        {shown.map((p) => (
          <button key={p.id} onClick={() => onSelect(p)} aria-pressed={p.id === selectedId}
            className={clsx('w-full rounded-lg border p-3 text-left transition-colors', p.id === selectedId ? 'border-accent bg-accent/10' : 'border-line hover:border-accent/50')}>
            <div className="flex items-center justify-between gap-2">
              <span className="font-medium">{p.title}</span>
              <div className="flex shrink-0 items-center gap-1.5">
                {p.difficulty && <Badge tone={DIFFICULTY_TONE[p.difficulty] ?? 'neutral'}>{p.difficulty}</Badge>}
                <Badge tone="neutral">best {p.optimal_time}</Badge>
              </div>
            </div>
            <div className="mt-1.5 flex items-center gap-2 text-xs text-mute">
              {p.category && <span className="rounded bg-raised px-1.5 py-0.5 font-medium">{p.category}</span>}
              <span>Input {p.input_type}, output {p.output_type}, n up to {fmtN(p.max_n)}</span>
            </div>
          </button>
        ))}
        {shown.length === 0 && <p className="py-4 text-center text-sm text-mute">No problem matches "{q}". Switch to Custom to describe your own.</p>}
      </div>
      {statement && <p className="mt-4 rounded-lg bg-raised p-3 text-sm text-mute"><span className="font-medium text-ink">Statement. </span>{statement}</p>}
    </div>
  );
}