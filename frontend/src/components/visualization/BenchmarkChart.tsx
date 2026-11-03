import { Bar, BarChart, CartesianGrid, Cell, LabelList, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { fmtMs } from '../../lib/format';
import type { Benchmark } from '../../types/api';

/** Measured runtime before and after, from the Benchmark step. Log scale because the gap is usually large. */
export default function BenchmarkChart({ bench }: { bench: Benchmark }) {
  const data = [{ name: 'Original', ms: bench.before_ms, c: 'rgb(var(--mute))' }, { name: 'Optimized', ms: bench.after_ms, c: 'rgb(var(--accent))' }];
  return (
    <div>
      <div style={{ height: 240 }} role="img" aria-label="Measured runtime before and after, log scale">
        <ResponsiveContainer>
          <BarChart data={data} layout="vertical" margin={{ top: 4, right: 70, bottom: 4, left: 8 }}>
            <CartesianGrid stroke="rgb(var(--line))" strokeDasharray="3 3" horizontal={false} />
            <XAxis type="number" scale="log" domain={['auto', 'auto']} stroke="rgb(var(--mute))" tick={{ fontSize: 12 }} tickFormatter={(v: number) => `${v} ms`} />
            <YAxis type="category" dataKey="name" stroke="rgb(var(--mute))" tick={{ fontSize: 13 }} width={90} />
            <Tooltip formatter={(v: number) => fmtMs(v)} contentStyle={{ background: 'rgb(var(--panel))', border: '1px solid rgb(var(--line))', borderRadius: 8 }} />
            <Bar dataKey="ms" isAnimationActive={false} barSize={34} radius={4}>
              {data.map((d) => <Cell key={d.name} fill={d.c} />)}
              <LabelList dataKey="ms" position="right" formatter={(v: number) => fmtMs(v)} style={{ fill: 'rgb(var(--ink))', fontSize: 12 }} />
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      </div>
      <p className="mt-2 text-sm text-mute">Input size n = {bench.n_used.toLocaleString('en-US')}. Both timings come from the same machine in the same run. Log scale.</p>
    </div>
  );
}
