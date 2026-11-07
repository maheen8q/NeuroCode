import type {
  AuthResponse, CreateJobRequest, CreateJobResponse, ErrorEnvelope, ExecuteRequest, ExecuteResponse, Health, Job, JobPage,
  ModelInfo, Problem, TrainRequest, User,
} from '../types/api';
import { ApiError } from './ApiError';
import { E } from './endpoints';
import { mockHandle } from './mock';
// The backend names a job's id `id`; the frontend types and pages use `job_id`. Fill it in once here
// so every page and hook (live progress socket, links, cancel) gets a real id.
const withJobId = <T extends { job_id?: number; id?: number }>(j: T): T => ({ ...j, job_id: j.job_id ?? j.id } as T);
export { ApiError };
export const USE_MOCK = import.meta.env.VITE_USE_MOCK === 'true';
const BASE = import.meta.env.VITE_API_BASE ?? '';
const TOKEN_KEY = 'nc.token';

export const getToken = () => localStorage.getItem(TOKEN_KEY);
export const setToken = (t: string | null) => (t ? localStorage.setItem(TOKEN_KEY, t) : localStorage.removeItem(TOKEN_KEY));

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  if (USE_MOCK) return (await mockHandle(method, path, body)) as T;
  const token = getToken();
  const res = await fetch(BASE + path, {
    method,
    headers: {
      Accept: 'application/json',
      ...(body !== undefined ? { 'Content-Type': 'application/json; charset=utf-8' } : {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) {
    const env = (await res.json().catch(() => null)) as Partial<ErrorEnvelope> | null;
    const e = env?.error;
    if (res.status === 401 && !path.includes('/auth/login') && !path.includes('/auth/signup')) window.dispatchEvent(new Event('nc:unauthorized'));
    throw new ApiError(res.status, e?.code ?? 'INTERNAL', e?.message ?? (res.statusText || `Request failed (${res.status})`), e?.field);
  }
  const text = await res.text();
  return (text ? JSON.parse(text) : undefined) as T;
}

export interface JobQuery { limit: number; offset: number; type?: string; status?: string }

export const api = {
  health: () => request<Health>('GET', E.health),
  auth: {
    login: (username: string, password: string) => request<AuthResponse>('POST', E.login, { username, password }),
    signup: (username: string, password: string) => request<AuthResponse>('POST', E.signup, { username, password }),
    logout: () => request<void>('POST', E.logout),
    me: () => request<User>('GET', E.me),
  },
  problems: {
    list: async () => (await request<{ problems: Problem[] }>('GET', E.problems)).problems,
    get: (id: string) => request<Problem>('GET', E.problem(id)),
  },
  jobs: {
    create: (b: CreateJobRequest) => request<CreateJobResponse>('POST', E.jobs, b),
    get: async (id: number) => withJobId(await request<Job>('GET', E.job(id))),
    list: async (q: JobQuery) => {
      const p = new URLSearchParams({ limit: String(q.limit), offset: String(q.offset) });
      if (q.type) p.set('type', q.type);
      if (q.status) p.set('status', q.status);
            const page = await request<JobPage>('GET', `${E.jobs}?${p}`);
      return { ...page, jobs: page.jobs.map(withJobId) };
    },
    cancel: (id: number) => request<void>('DELETE', E.job(id)),
  },
  execute: (b: ExecuteRequest) => request<ExecuteResponse>('POST', E.execute, b),
  train: (b: TrainRequest) => request<CreateJobResponse>('POST', E.train, b),
  models: {
    list: async () => (await request<{ models: ModelInfo[] }>('GET', E.models)).models,
    activate: (id: number) => request<ModelInfo>('POST', E.activate(id)),
  },
};
