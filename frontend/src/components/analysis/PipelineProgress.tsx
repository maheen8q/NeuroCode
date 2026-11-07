import clsx from 'clsx';
import { Check } from 'lucide-react';
import { PIPELINE, stageIndex } from '../../lib/pipeline';

const ITEM = 96;          // height of one node row, px
const VIEW = ITEM * 5;    // visible window: active node plus two above and two below
const NODE = 56;          // circle diameter, px
const PAD = 32;           // left padding of each row, px

export default function PipelineProgress({ percent, stage, status }: { percent: number; stage: string; status: string }) {
  const queued = status === 'queued';
  const finished = percent >= 100;
  const total = PIPELINE.length;
  const current = finished ? total - 1 : queued ? 0 : Math.max(0, stageIndex(percent));
  const offset = VIEW / 2 - ITEM / 2 - current * ITEM;

  return (
    <div className="rounded-xl border border-line bg-panel p-6">
      <div className="relative mx-auto max-w-md overflow-hidden" style={{ height: VIEW }}>
        <div className="pointer-events-none absolute inset-x-0 top-0 z-20 h-24 bg-gradient-to-b from-panel to-transparent" />
        <div className="pointer-events-none absolute inset-x-0 bottom-0 z-20 h-24 bg-gradient-to-t from-panel to-transparent" />

        <ol className="relative z-10 transition-transform duration-700 ease-[cubic-bezier(0.22,1,0.36,1)]" style={{ transform: `translateY(${offset}px)` }}>
          {PIPELINE.map((s, i) => {
            const done = finished || i < current;
            const active = !finished && i === current;
            const distance = Math.abs(i - current);
            const isLast = i === total - 1;

            return (
              <li key={s.label} className="relative flex items-center transition-opacity duration-700"
                style={{ height: ITEM, paddingLeft: PAD, opacity: distance === 0 ? 1 : distance === 1 ? 0.55 : 0.25 }}>
                {!isLast && (
                  <span className="absolute w-0.5 overflow-hidden rounded-full bg-line"
                    style={{ left: PAD + NODE / 2 - 1, top: ITEM / 2 + NODE / 2 + 4, height: ITEM - NODE - 8 }}>
                    <span className={clsx('block w-full bg-accent transition-all duration-700', active && 'animate-pulse')}
                      style={{ height: done ? '100%' : active ? '50%' : '0%' }} />
                  </span>
                )}

                <span
                  className={clsx(
                    'flex shrink-0 items-center justify-center rounded-full border-2 text-lg font-bold transition-all duration-500',
                    done && 'border-accent bg-accent text-bg',
                    active && 'animate-ring border-accent bg-accent/15 text-accent shadow-[0_0_28px_rgb(var(--accent)/0.6)]',
                    !done && !active && 'border-line text-mute',
                  )}
                  style={{ width: NODE, height: NODE, transform: `scale(${active ? 1.1 : distance === 1 ? 0.85 : 0.75})` }}
                >
                  {done ? <Check className="h-6 w-6" /> : i + 1}
                </span>

                <div className="ml-5 min-w-0">
                  <p className={clsx('font-semibold transition-all duration-500', active ? 'text-xl text-ink' : 'text-base', done && !active ? 'text-ink' : !active && 'text-mute')}>
                    {s.label}
                  </p>
                  {active && (
                    <p className="mt-0.5 flex items-center gap-1.5 text-sm text-accent">
                      {queued ? 'waiting for a worker' : stage || 'working'}
                      <span className="inline-flex gap-0.5" aria-hidden="true">
                        <span className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent [animation-delay:-0.3s]" />
                        <span className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent [animation-delay:-0.15s]" />
                        <span className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent" />
                      </span>
                    </p>
                  )}
                  {done && !active && <p className="mt-0.5 text-sm text-mute">done</p>}
                </div>
              </li>
            );
          })}
        </ol>
      </div>

      <div className="mx-auto mt-6 max-w-md">
        <div className="mb-2 flex items-baseline justify-between text-sm">
          <span className="text-mute">{queued ? 'Queued' : finished ? 'Complete' : `Stage ${current + 1} of ${total}`}</span>
          <span className="font-display text-2xl font-bold tabular-nums">{percent}%</span>
        </div>
        <div className="h-1.5 overflow-hidden rounded-full bg-raised">
          <div className="h-full rounded-full bg-accent transition-all duration-700 ease-out" style={{ width: `${percent}%` }} />
        </div>
      </div>
    </div>
  );
}