import { ScanSearch } from 'lucide-react';
import { useCallback, useEffect, useState } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { api } from '../api/client';
import { Button, Card, ErrorBox, PageHeader, Segmented } from '../components/common/ui';
import CodeEditor from '../components/editor/CodeEditor';
import EditorToolbar from '../components/editor/EditorToolbar';
import ProblemForm, { BLANK_SPEC } from '../components/problem/ProblemForm';
import ProblemPicker from '../components/problem/ProblemPicker';
import { errorMessage } from '../lib/errors';
import { fmtKb, fmtMs } from '../lib/format';
import { DEFAULT_CODE } from '../lib/samples';
import { MAX_CODE_BYTES, codeBytes, submitJob } from '../lib/submit';
import type { ExecuteResponse, Problem, ProblemSpec } from '../types/api';

const DRAFT = 'nc.draft';

export default function Analyzer() {
  const nav = useNavigate();
  const incoming = (useLocation().state as { code?: string } | null)?.code;
  const [code, setCode] = useState(() => incoming ?? localStorage.getItem(DRAFT) ?? DEFAULT_CODE);
  const [mode, setMode] = useState<'catalog' | 'custom'>('catalog');
  const [picked, setPicked] = useState<Problem | null>(null);
  const [custom, setCustom] = useState<ProblemSpec>(BLANK_SPEC);
  const [pendingProblem, setPendingProblem] = useState<string | undefined>('pair_sum');
  const [submitting, setSubmitting] = useState(false);
  const [err, setErr] = useState('');
  const [console_, setConsole] = useState(false);
  const [stdin, setStdin] = useState('');
  const [running, setRunning] = useState(false);
  const [out, setOut] = useState<ExecuteResponse | null>(null);
  const [runErr, setRunErr] = useState('');

  useEffect(() => { localStorage.setItem(DRAFT, code); }, [code]);
  const onSelect = useCallback((p: Problem) => setPicked(p), []);

  const empty = code.trim().length === 0;
  const size = codeBytes(code);
  const tooLarge = size > MAX_CODE_BYTES;
  const ready = !empty && !tooLarge && (mode === 'custom' || picked !== null);

  const analyze = async () => {
    setErr(''); setSubmitting(true);
    try {
      const id = await submitJob('analyze', mode === 'catalog'
        ? { code, problem_id: picked!.id, problem: null }
        : { code, problem_id: null, problem: custom });
      nav(`/jobs/${id}`);
    } catch (e) { setErr(errorMessage(e)); setSubmitting(false); }
  };
  const run = async () => {
    setConsole(true); setRunning(true); setRunErr('');
    try { setOut(await api.execute({ language: 'cpp', code, stdin, timeout_ms: 5000 })); } catch (e) { setOut(null); setRunErr(errorMessage(e)); }
    finally { setRunning(false); }
  };

  return (
    <>
      <PageHeader title="Analyze code" description="Submit a C++ solution and describe the problem it solves. NeuroCode derives its complexity, classifies the algorithm, and tests it against a reference solution." />
      <div className="grid gap-6 xl:grid-cols-[minmax(0,1fr)_380px]">
        <div>
          <EditorToolbar running={running} consoleOpen={console_} onToggleConsole={() => setConsole((c) => !c)} onRun={() => void run()}
            onClear={() => setCode('')} onPick={(s) => { setCode(s.code); setMode('catalog'); setPicked(null); setPendingProblem(s.problem || undefined); }} />
          <CodeEditor value={code} onChange={setCode} height={console_ ? '400px' : '560px'} />
          <p className={`mt-2 text-xs ${tooLarge ? 'text-bad' : 'text-mute'}`}>{(size / 1024).toFixed(1)} KB of 64 KB{tooLarge ? '. The server rejects sources above 64 KB.' : ''}</p>
          {console_ && (
            <div className="mt-3 grid gap-3 rounded-lg border border-line bg-panel p-4 md:grid-cols-[240px_1fr]">
              <div><label className="label" htmlFor="stdin">Program input (stdin)</label><textarea id="stdin" rows={5} className="field font-mono" value={stdin} onChange={(e) => setStdin(e.target.value)} placeholder="Optional" /></div>
              <div>
                <p className="label">Output</p>
                {runErr && <ErrorBox title="The run request failed">{runErr}</ErrorBox>}
                {!out && !runErr && <p className="text-sm text-mute">Run the code to see its output. It runs in a sandbox with a 5 second limit.</p>}
                {out && (
                  <div className="space-y-2 text-sm">
                    {!out.compiled ? <pre className="overflow-x-auto rounded-md bg-bad/10 p-3 font-mono text-bad">{out.compile_error}</pre> : (
                      <>
                        <pre className="max-h-32 overflow-auto rounded-md bg-raised p-3 font-mono">{out.stdout || '(no output)'}</pre>
                        {out.stderr && <pre className="overflow-x-auto rounded-md bg-warn/10 p-3 font-mono text-warn">{out.stderr}</pre>}
                        <p className="text-mute">{out.timed_out ? 'Stopped: time limit reached.' : `Exit code ${out.exit_code}, ${fmtMs(out.execution_time_ms)}, ${fmtKb(out.memory_kb)}`}</p>
                      </>
                    )}
                  </div>
                )}
              </div>
            </div>
          )}
        </div>

        <aside className="space-y-4">
          <Card title="Problem" subtitle="Used to pick the reference solution for testing.">
            <div className="mb-4"><Segmented value={mode} onChange={setMode} label="Problem source" options={[{ value: 'catalog', label: 'From catalog' }, { value: 'custom', label: 'Custom' }]} /></div>
            {mode === 'catalog' ? <ProblemPicker selectedId={picked?.id ?? ''} onSelect={onSelect} initialId={pendingProblem} /> : <ProblemForm value={custom} onChange={setCustom} />}
          </Card>
          {err && <ErrorBox title="Could not start the analysis">{err}</ErrorBox>}
          <Button size="lg" className="w-full" icon={<ScanSearch className="h-5 w-5" />} loading={submitting} disabled={!ready} onClick={() => void analyze()}>Analyze code</Button>
          {mode === 'catalog' && !picked && <p className="text-center text-sm text-mute">Choose a problem, or switch to Custom.</p>}
          {empty && <p className="text-center text-sm text-mute">The editor is empty. Paste code or load an example.</p>}
        </aside>
      </div>
    </>
  );
}
