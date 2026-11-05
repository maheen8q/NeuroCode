import type { ReactNode } from 'react';
import type { AnalysisResult } from '../../types/api';

const Metric = ({ label, value, note, tone }: { label: string; value: ReactNode; note: string; tone: string }) => (
  <div className="rounded-xl border border-line bg-panel p-5">
    <p className="text-sm text-mute">{label}</p>
    <p className={`mt-1 font-display text-3xl font-bold tabular-nums ${tone}`}>{value}</p>
    <p className="mt-1 text-sm text-mute">{note}</p>
  </div>
);

/** Three numbers the backend actually returns: counterexamples, suspicious patterns, and whether
 * differential testing found any mismatches. The backend does not report a total test count, so
 * this never claims an "X/Y tests" ratio, only whether any counterexample was found. */
export default function MetricsBar({ r }: { r: AnalysisResult }) {
  const high = r.findings.filter((f) => f.severity === 'high').length;
  const failing = r.counterexamples.length > 0;
  return (
    <div className="grid gap-4 sm:grid-cols-3">
      <Metric label="Differential testing" value={failing ? 'Mismatches found' : 'No mismatches'} tone={failing ? 'text-bad' : 'text-ink'}
        note={failing ? `${r.counterexamples.length} input(s) differ from the reference` : 'Matched the reference on every generated input'} />
      <Metric label="Suspicious patterns" value={r.findings.length} tone={high ? 'text-bad' : 'text-ink'} note={high ? `${high} high severity` : 'None high severity'} />
      <Metric label="Counterexamples" value={r.counterexamples.length} tone={r.counterexamples.length ? 'text-bad' : 'text-ink'}
        note={r.counterexamples.length ? 'Inputs where the output is wrong' : 'None found'} />
    </div>
  );
}
