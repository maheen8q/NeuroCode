import { useMemo, useState } from 'react';
import type { AnalysisResult } from '../../types/api';
import { Card, ErrorBox, Tabs } from '../common/ui';
import CodeEditor from '../editor/CodeEditor';
import ASTViewer from '../visualization/ASTViewer';
import CFGViewer from '../visualization/CFGViewer';
import AlgorithmCard from './AlgorithmCard';
import ComplexityCard from './ComplexityCard';
import CounterexampleCard from './CounterexampleCard';
import FindingsList from './FindingsList';
import MetricsBar from './MetricsBar';

type Tab = 'overview' | 'code' | 'structure';

/** The analysis half of a result. Used by the Results page and, below the optimization, by the Optimization page. */
export default function AnalysisView({ r, code }: { r: AnalysisResult; code: string | null }) {
  const [tab, setTab] = useState<Tab>('overview');
  const [graph, setGraph] = useState<'cfg' | 'ast'>('cfg');
  const markers = useMemo(() => r.findings.map((f) => ({ line: f.line, message: f.message, severity: f.severity })), [r.findings]);

  return (
    <div className="space-y-6">
      {r.execution && !r.execution.compiled && (
        <ErrorBox title="The code did not compile">{r.execution.compile_error && <pre className="mt-1 overflow-x-auto font-mono text-xs">{r.execution.compile_error}</pre>}</ErrorBox>
      )}
      <MetricsBar r={r} />
      <Tabs value={tab} onChange={setTab} tabs={[{ value: 'overview', label: 'Overview' }, { value: 'code', label: 'Code' }, { value: 'structure', label: 'Structure' }]} />
      {tab === 'overview' && (
        <div className="grid gap-6 lg:grid-cols-2">
          <ComplexityCard complexity={r.complexity} />
          <AlgorithmCard algorithms={r.algorithms} lowConfidence={r.low_confidence} modelVersion={r.model_version} />
          <FindingsList findings={r.findings} onJump={() => setTab('code')} />
          <CounterexampleCard items={r.counterexamples} testsRun={r.execution?.tests_run} />
        </div>
      )}
      {tab === 'code' && (
        <Card title="Submitted code" subtitle={code === null ? undefined : r.findings.length ? 'Lines with suspicious patterns are underlined. Hover to read them.' : 'No patterns to mark.'}>
          {code === null
            ? <p className="text-mute">The source for this job is not stored in this browser. It was probably submitted from another browser or device.</p>
            : <CodeEditor value={code} readOnly markers={markers} height="480px" />}
        </Card>
      )}
      {tab === 'structure' && (
        <Card title={graph === 'cfg' ? 'Control flow graph' : 'Syntax tree'} subtitle="The engine's internal view of the code"
          action={<div className="flex gap-1 rounded-lg border border-line bg-raised p-0.5">{(['cfg', 'ast'] as const).map((g) => <button key={g} onClick={() => setGraph(g)} aria-pressed={graph === g} className={`rounded-md px-3 py-1 text-sm ${graph === g ? 'bg-panel font-medium' : 'text-mute'}`}>{g.toUpperCase()}</button>)}</div>}>
          {graph === 'cfg' ? <CFGViewer graph={r.cfg} /> : <ASTViewer tree={r.ast} />}
        </Card>
      )}
    </div>
  );
}
