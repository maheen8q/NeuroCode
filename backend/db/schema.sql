-- NeuroCode schema (architecture.md section 6). Safe to run repeatedly.
-- Timestamps are written by the C++ repos as ISO 8601 UTC ("2026-04-12T09:31:07Z").
-- The CURRENT_TIMESTAMP defaults are only a fallback for manual inserts.

CREATE TABLE IF NOT EXISTS users (            -- stub auth for now: one seeded user
    id            INTEGER PRIMARY KEY,
    username      TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,              -- opaque; no hashing library yet
    created_at    TEXT DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    expires_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS problems (         -- seeded catalog (db/seed.sql)
    id             TEXT PRIMARY KEY,          -- "pair_sum"
    title          TEXT NOT NULL,
    description    TEXT,                      -- shown in the UI (api "statement")
    input_type     TEXT,
    output_type    TEXT,
    operation      TEXT,
    max_n          INTEGER,
    optimal_time   TEXT,                      -- "O(n)"
    reference_code TEXT NOT NULL              -- solve(...) convention, UNCONFIRMED with T4
);

CREATE TABLE IF NOT EXISTS jobs (
    id           INTEGER PRIMARY KEY,
    user_id      INTEGER REFERENCES users(id),
    type         TEXT CHECK(type IN ('analyze','optimize','train')),
    status       TEXT CHECK(status IN ('queued','running','done','failed','cancelled')),
    problem_id   TEXT REFERENCES problems(id),  -- catalog problem, or NULL
    problem_json TEXT,                          -- custom ProblemSpec, or NULL
    input_code   TEXT,
    progress     INTEGER DEFAULT 0,
    stage        TEXT,
    result_json  TEXT,                          -- whole result: algorithms, complexity, findings,
                                                -- counterexamples, optimization, ast, cfg
    error        TEXT,                          -- JSON {"code": "...", "message": "..."}
    created_at   TEXT DEFAULT CURRENT_TIMESTAMP,
    finished_at  TEXT
);
CREATE INDEX IF NOT EXISTS idx_jobs_user   ON jobs(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status);

CREATE TABLE IF NOT EXISTS models (            -- read-only cache of models/*.pt + sidecar .json
    id              INTEGER PRIMARY KEY,
    version         TEXT UNIQUE NOT NULL,       -- "v20261028-0013"
    checkpoint_path TEXT NOT NULL,
    accuracy        REAL,
    loss            REAL,                       -- NULL: the sidecar does not store it
    epochs          INTEGER,
    dataset_size    INTEGER,
    trained_at      TEXT DEFAULT CURRENT_TIMESTAMP,
    is_active       INTEGER DEFAULT 0           -- newest checkpoint by filename sort
);
