const V1 = '/api/v1';
export const E = {
  health: `${V1}/health`,
  login: `${V1}/auth/login`,
  signup: `${V1}/auth/signup`,
  logout: `${V1}/auth/logout`,
  me: `${V1}/auth/me`,
  problems: `${V1}/problems`,
  problem: (id: string) => `${V1}/problems/${encodeURIComponent(id)}`,
  jobs: `${V1}/jobs`,
  job: (id: number) => `${V1}/jobs/${id}`, // GET reads, DELETE cancels
  execute: `${V1}/execute`,
  train: `${V1}/models/train`,
  models: `${V1}/models`,
  activate: (id: number) => `${V1}/models/${id}/activate`,
  ws: (id: number) => `${V1}/ws/jobs/${id}`,
};
