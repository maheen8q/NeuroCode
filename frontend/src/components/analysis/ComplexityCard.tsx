import type { Complexity } from '../../types/api';
import { Badge, Card } from '../common/ui';

const Big = ({ label, value }: { label: string; value: string }) => (
  <div className="rounded-lg bg-raised px-4 py-3">
    <p className="text-sm text-mute">{label}</p>
    <p className="font-mono text-2xl font-medium">{value || 'unknown'}</p>
  </div>
);

// A line counts as "formula-shaped" if it reads like math rather than prose:
// contains Big-O notation, an equals sign, or a recurrence like T(n).
function isFormula(line: string): boolean {
  return /O\(|T\(n\)|=/.test(line) && line.length < 60;
}

function isSectionHeader(line: string): boolean {
  return line === 'Time:' || line === 'Space:';
}

export default function ComplexityCard({ complexity }: { complexity: Complexity }) {
  const unknown = !complexity.time || complexity.time === 'unknown';
  return (
    <Card title="Complexity" subtitle="Derived by rules, not predicted by the model." action={<Badge tone="ok">Deterministic</Badge>}>
      <div className="grid grid-cols-2 gap-3"><Big label="Time" value={complexity.time} /><Big label="Space" value={complexity.space} /></div>
      {unknown && <p className="mt-3 text-sm text-mute">The engine does not recognise this pattern, so it reports unknown instead of guessing.</p>}
      {complexity.derivation.length > 0 && (
        <div className="mt-5">
          <h3 className="mb-3 text-sm font-bold">How the engine got there</h3>
          <div className="relative space-y-0">
            {complexity.derivation.map((d, i) => {
              const isLast = i === complexity.derivation.length - 1;
              const header = isSectionHeader(d);
              const formula = !header && isFormula(d);
              return (
                <div key={i} className="relative flex gap-3 pb-4">
                  {!isLast && (
                    <span className="absolute left-[9px] top-5 h-full w-px bg-line" aria-hidden="true" />
                  )}
                  <span
                    className={`relative z-10 mt-0.5 flex h-[18px] w-[18px] shrink-0 items-center justify-center rounded-full text-[10px] font-bold ${
                      header
                        ? 'bg-ink text-panel'
                        : formula
                        ? 'bg-accent text-white'
                        : 'bg-accent/15 text-accent'
                    }`}
                  >
                    {header ? '' : formula ? 'Σ' : i + 1}
                  </span>
                  {header ? (
                    <span className="pt-0.5 text-sm font-bold uppercase tracking-wide text-mute">{d}</span>
                  ) : formula ? (
                    <span className="rounded-md bg-raised px-3 py-1.5 font-mono text-sm font-medium text-ink">{d}</span>
                  ) : (
                    <span className="pt-0.5 text-sm text-mute">{d}</span>
                  )}
                </div>
              );
            })}
          </div>
        </div>
      )}
    </Card>
  );
}