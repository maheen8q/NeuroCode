import type { ReactNode } from 'react';
import type { AnalysisResult } from '../../types/api';

const Metric = ({ label, value, note, tone }: { label: string; value: ReactNode; note: string; tone: string }) => (
  <div className="rounded-xl border border-line bg-panel p-5">
    <p className="text-sm text-mute">{label}</p>
    <p className={`mt-1 font-display text-3xl font-bold tabular-nums ${tone}`}>{value}</p>
    <p className="mt-1 text-sm text-mute">{note}</p>
  </div>
);

/** Three numbers the backend actually returns: test results, suspicious patterns, counterexamples. */
export default function MetricsBar({ r }: { r: AnalysisResult }) {
  const ex = r.execution;
  const high = r.findings.filter((f) => f.severity === 'high').length;
  const failing = ex && ex.compiled && ex.tests_passed < ex.tests_run;
  return (
    <div className="grid gap-4 sm:grid-cols-3">
      <Metric label="Generated tests passed" value={ex?.compiled ? `${ex.tests_passed}/${ex.tests_run}` : 'None run'} tone={!ex?.compiled || failing ? 'text-bad' : 'text-ink'}
        note={!ex?.compiled ? 'The code did not compile' : failing ? `${ex.tests_run - ex.tests_passed} differ from the reference` : 'All matched the reference'} />
      <Metric label="Suspicious patterns" value={r.findings.length} tone={high ? 'text-bad' : 'text-ink'} note={high ? `${high} high severity` : 'None high severity'} />
      <Metric label="Counterexamples" value={r.counterexamples.length} tone={r.counterexamples.length ? 'text-bad' : 'text-ink'}
        note={r.counterexamples.length ? 'Inputs where the output is wrong' : 'None found'} />
    </div>
  );
}
