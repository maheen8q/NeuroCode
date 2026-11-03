import { useEffect, useState } from 'react';
import { api } from '../api/client';
import type { Health } from '../types/api';

export type HealthState = { state: 'loading' | 'offline' } | { state: 'ok' | 'degraded'; data: Health };

export function useHealth(intervalMs = 15000): HealthState {
  const [h, setH] = useState<HealthState>({ state: 'loading' });
  useEffect(() => {
    let alive = true;
    const tick = () => api.health().then((data) => alive && setH({ state: data.status, data })).catch(() => alive && setH({ state: 'offline' }));
    tick();
    const t = window.setInterval(tick, intervalMs);
    return () => { alive = false; window.clearInterval(t); };
  }, [intervalMs]);
  return h;
}
