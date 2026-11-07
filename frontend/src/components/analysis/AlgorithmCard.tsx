import { Info } from 'lucide-react';
import { Cell, Pie, PieChart, ResponsiveContainer, Tooltip } from 'recharts';
import { fmtPct } from '../../lib/format';
import { humanize } from '../../lib/labels';
import type { AlgorithmGuess } from '../../types/api';
import { Card } from '../common/ui';

const COLORS = ['rgb(var(--accent))', 'rgb(var(--mute) / 0.55)', 'rgb(var(--mute) / 0.3)', 'rgb(var(--mute) / 0.18)'];

export default function AlgorithmCard({ algorithms, lowConfidence, modelVersion }: { algorithms: AlgorithmGuess[]; lowConfidence: boolean; modelVersion: string }) {
  const top = algorithms[0];
  const data = algorithms.map((a) => ({ name: humanize(a.label), value: a.probability }));

  return (
    <Card title="Algorithm classification" subtitle={`Ranked predictions from the classifier${modelVersion ? `, model ${modelVersion}` : ''}`}>
      {algorithms.length === 0 ? (
        <p className="text-mute">The model returned no prediction for this code.</p>
      ) : (
        <div className="flex flex-col items-center">
          <div className="relative" style={{ width: 260, height: 260 }} role="img" aria-label={`Top prediction: ${humanize(top.label)} at ${fmtPct(top.probability, 1)}`}>
            <ResponsiveContainer>
              <PieChart>
                <Pie data={data} dataKey="value" nameKey="name" innerRadius={88} outerRadius={126} startAngle={90} endAngle={-270} stroke="none" isAnimationActive={false}>
                  {data.map((d, i) => <Cell key={d.name} fill={COLORS[i] ?? COLORS[COLORS.length - 1]} />)}
                </Pie>
                <Tooltip formatter={(v: number) => fmtPct(v, 1)} contentStyle={{ background: 'rgb(var(--panel))', border: '1px solid rgb(var(--line))', borderRadius: 8 }} />
              </PieChart>
            </ResponsiveContainer>
            <div className="pointer-events-none absolute inset-0 flex flex-col items-center justify-center text-center">
              <span className="text-4xl font-bold leading-tight">{fmtPct(top.probability, 0)}</span>
              <span className="mt-1 text-sm text-mute">{humanize(top.label)}</span>
            </div>
          </div>

          <ul className="mt-6 w-full max-w-xs space-y-2.5">
            {algorithms.map((a, i) => (
              <li key={a.label} className="flex items-center gap-2.5">
                <span className="h-2.5 w-2.5 shrink-0 rounded-full" style={{ background: COLORS[i] ?? COLORS[COLORS.length - 1] }} />
                <span className={`flex-1 text-sm ${i === 0 ? 'font-medium' : 'text-mute'}`}>{humanize(a.label)}</span>
                <span className="tabular-nums text-sm text-mute">{fmtPct(a.probability, 1)}</span>
              </li>
            ))}
          </ul>
        </div>
      )}
      {lowConfidence && (
        <p className="mt-4 flex gap-2 rounded-lg bg-warn/10 p-3 text-sm text-warn"><Info className="mt-0.5 h-4 w-4 shrink-0" />
          Low confidence. Rely on the complexity result, which is derived by rules rather than predicted.</p>
      )}
    </Card>
  );
}