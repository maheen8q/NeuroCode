import { Bar, BarChart, CartesianGrid, Cell, LabelList, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';

// Static facts, not fetched: each pass's before/after complexity class is a fixed
// property of that pass's code, declared once and never computed at runtime.
const PASSES = [
  { name: 'Pair sum', before: 'O(n²)', after: 'O(n)', beforeLevel: 6, afterLevel: 2 },
  { name: 'Sorted search', before: 'O(n)', after: 'O(log n)', beforeLevel: 2, afterLevel: 1 },
  { name: 'Range sum', before: 'O(n·q)', after: 'O(n+q)', beforeLevel: 5, afterLevel: 2 },
  { name: 'Fibonacci', before: 'O(2ⁿ)', after: 'O(n)', beforeLevel: 8, afterLevel: 2 },
];

const data = PASSES.flatMap((p) => [
  { name: `${p.name} (before)`, pass: p.name, level: p.beforeLevel, label: p.before, c: 'rgb(var(--mute))' },
  { name: `${p.name} (after)`, pass: p.name, level: p.afterLevel, label: p.after, c: 'rgb(var(--accent))' },
]);

export default function OptimizationsChart() {
  return (
    <div>
      <p className="mb-2 text-xs font-medium uppercase tracking-wide text-mute">Four verified optimizations</p>
      <div style={{ height: 200 }} role="img" aria-label="Complexity class before and after, for each of the four implemented optimizer passes">
        <ResponsiveContainer>
          <BarChart data={data} layout="vertical" margin={{ top: 0, right: 56, bottom: 0, left: 8 }} barCategoryGap={10}>
            <CartesianGrid stroke="rgb(var(--line))" strokeDasharray="3 3" horizontal={false} />
            <XAxis type="number" hide domain={[0, 9]} />
            <YAxis type="category" dataKey="pass" stroke="rgb(var(--mute))" tick={{ fontSize: 12 }} width={92}
              tickFormatter={(v: string) => v} interval={0} allowDuplicatedCategory />
            <Tooltip formatter={(_v: number, _k: string, item: { payload?: { label?: string } }) => item?.payload?.label ?? ''}
              contentStyle={{ background: 'rgb(var(--panel))', border: '1px solid rgb(var(--line))', borderRadius: 8 }} />
            <Bar dataKey="level" isAnimationActive={false} barSize={8} radius={3}>
              {data.map((d) => <Cell key={d.name} fill={d.c} />)}
              <LabelList dataKey="label" position="right" style={{ fill: 'rgb(var(--ink))', fontSize: 11, fontFamily: 'monospace' }} />
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      </div>
      <p className="mt-1 text-xs text-mute">Grey = original complexity, purple = optimized. Each is detected, rewritten, and verified against generated tests before it's ever shown.</p>
    </div>
  );
}