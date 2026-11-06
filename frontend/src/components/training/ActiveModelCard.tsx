import { Cpu } from 'lucide-react';
import { Badge, Card } from '../common/ui';
import { fmtN, fmtPct, relTime } from '../../lib/format';
import type { ModelInfo } from '../../types/api';

const Stat = ({ label, value }: { label: string; value: string }) => (
  <div className="rounded-lg bg-raised px-4 py-3">
    <p className="text-sm text-mute">{label}</p>
    <p className="font-display text-xl font-bold tabular-nums">{value}</p>
  </div>
);

/** What powers the analysis right now: the active model's own numbers, pulled from the same
 * list the Saved models table uses. Shown as a dedicated panel instead of a table row, so a
 * viewer can see at a glance what the classifier actually is before they trust a prediction. */
export default function ActiveModelCard({ models }: { models: ModelInfo[] | null }) {
  const active = models?.find((m) => m.is_active) ?? null;

  if (!models) return null;
  if (!active) {
    return (
      <Card title="Active model" subtitle="What powers the algorithm classification in Analyze.">
        <p className="text-mute">No model is currently active. Train one and activate it to enable classification.</p>
      </Card>
    );
  }

  const acc = active.cv_accuracy ?? active.accuracy;

  return (
    <Card
      title="Active model"
      subtitle="What powers the algorithm classification in Analyze."
      action={<Badge tone="ok">In use</Badge>}
    >
      <div className="flex items-start gap-4">
        <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl bg-accent/15 text-accent">
          <Cpu className="h-5 w-5" />
        </div>
        <div>
          <p className="font-mono text-lg font-semibold leading-tight">{active.version}</p>
          <p className="text-sm text-mute">Feedforward classifier, ranked multi-class output</p>
        </div>
      </div>

      <div className="mt-5 grid grid-cols-2 gap-3 sm:grid-cols-4">
        <Stat label="CV accuracy" value={acc != null ? fmtPct(acc, 1) : '-'} />
        <Stat label="Training epochs" value={String(active.epochs)} />
        <Stat label="Training samples" value={fmtN(active.dataset_size)} />
        <Stat label="Last trained" value={relTime(active.trained_at)} />
      </div>

      <p className="mt-4 text-sm text-mute">
        Accuracy is cross-validated across folds, not a single held-out split, so it reflects how the
        model performs on data it has not directly trained on.
      </p>
    </Card>
  );
}