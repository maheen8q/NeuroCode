import type { ReactNode } from 'react';
import { fmtN, fmtSpeedup } from '../../lib/format';
import type { Benchmark, OptimizationSummary } from '../../types/api';
import { Card } from '../common/ui';

export default function SummaryCard({ s, verification, bench }: { s: OptimizationSummary; verification: ReactNode; bench: Benchmark | null }) {
  return (
    <Card>
      <div className="flex flex-wrap items-start justify-between gap-6">
        <div className="max-w-2xl">
          <div className="mb-3">{verification}</div>
          <h2 className="text-2xl font-bold">{s.headline}</h2>
          <p className="mt-2 text-mute">{s.explanation}</p>
        </div>
        {s.speedup > 0 && (
          <div className="rounded-xl bg-raised px-6 py-4 text-center">
            <p className="font-display text-4xl font-bold tabular-nums">{fmtSpeedup(s.speedup)}</p>
            <p className="text-sm text-mute">measured speedup</p>
            {bench && <p className="mt-0.5 text-xs text-mute">n = {fmtN(bench.n_used)}</p>}
          </div>
        )}
      </div>
      <div className="mt-5 grid gap-3 md:grid-cols-2">
        <p className="rounded-lg bg-raised p-3 text-sm"><span className="font-medium">Trade-off. </span><span className="text-mute">{s.tradeoff || 'No significant trade-off.'}</span></p>
        <p className="rounded-lg bg-raised p-3 text-sm"><span className="font-medium">How it was checked. </span><span className="text-mute">{s.verification}</span></p>
      </div>
    </Card>
  );
}
