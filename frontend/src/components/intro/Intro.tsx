import { Bug, CheckCircle2, FlaskConical, Play, ShieldCheck, Timer, Undo2, type LucideIcon } from 'lucide-react';
import { useEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react';
import { LogoMark } from '../layout/Logo';

/* Scene timeline (ms). Total is about 25 s. Edit the numbers to retime the whole intro. */
const SCENES = [
  { id: 'brand', ms: 2600 },
  { id: 'pipeline', ms: 3000 },
  { id: 'editor', ms: 4400 },
  { id: 'trust', ms: 3600 },
  { id: 'demo', ms: 7600 },
  { id: 'tag', ms: 1800 },
  { id: 'logo', ms: 2200 },
] as const;
const TOTAL = SCENES.reduce((a, s) => a + s.ms, 0);

/* ---------- small helpers ---------- */
const Blur = ({ children, delay = 0, className = '' }: { children: ReactNode; delay?: number; className?: string }) => (
  <span className={`intro-blur ${className}`} style={{ animationDelay: `${delay}ms` }}>{children}</span>
);
const Words = ({ text, delay = 0, step = 90, className = '' }: { text: string; delay?: number; step?: number; className?: string }) => (
  <>{text.split(' ').map((w, i) => <Blur key={i} delay={delay + i * step} className={`intro-w ${className}`}>{w}</Blur>)}</>
);

function useTyped(text: string, on: boolean, speed = 40) {
  const [n, setN] = useState(0);
  useEffect(() => {
    if (!on || n >= text.length) return;
    const t = window.setTimeout(() => setN(n + 1), speed);
    return () => window.clearTimeout(t);
  }, [on, n, text, speed]);
  return text.slice(0, n);
}

const TOKEN = /(#include <[^>]+>|\b(?:for|if|return|using|namespace|const)\b|\b(?:bool|int|size_t|vector|true|false)\b|\b\d+\b)/;
function Code({ line }: { line: string }) {
  return (
    <>
      {line.split(TOKEN).map((p, i) => {
        if (!p) return null;
        const c = p.startsWith('#include') ? 't-inc' : /^(for|if|return|using|namespace|const)$/.test(p) ? 't-kw' : /^(bool|int|size_t|vector|true|false)$/.test(p) ? 't-ty' : /^\d+$/.test(p) ? 't-num' : '';
        return <span key={i} className={c}>{p}</span>;
      })}
    </>
  );
}

function Scene({ ms, children }: { ms: number; children: ReactNode }) {
  return <div className="intro-scene" style={{ '--ms': `${ms}ms` } as CSSProperties}>{children}</div>;
}

/* ---------- scene 1: "Introducing" types, then morphs into the wordmark ---------- */
function Brand() {
  const typed = useTyped('Introducing', true, 70);
  const [morph, setMorph] = useState(false);
  useEffect(() => { const t = window.setTimeout(() => setMorph(true), 1500); return () => window.clearTimeout(t); }, []);
  return (
    <div className="relative flex h-24 w-full items-center justify-center">
      <span className={`intro-abs intro-mid ${morph ? 'intro-gone' : ''}`}>{typed}<i className="intro-caret" /></span>
      <span className={`intro-abs intro-word ${morph ? 'intro-here' : ''}`}>NeuroCode<em className="intro-pill">v1.0</em></span>
    </div>
  );
}

/* ---------- scene 2: tilted card with the analysis pipeline ---------- */
const STEPS = [['01', 'Parse', 'libclang AST'], ['02', 'Simplify', 'Internal tree'], ['03', 'Control flow', 'Basic blocks'], ['04', 'Complexity', 'Derived by rules'], ['05', 'Predict', 'Ranked by the model'], ['06', 'Static checks', 'Suspicious patterns'], ['07', 'Verify', 'Against a reference']];
function Pipeline() {
  return (
    <div className="intro-card intro-tilt" style={{ width: 'min(520px, 88vw)', padding: 28 }}>
      <p className="intro-word-sm">NeuroCode <em className="intro-pill">v1.0</em></p>
      <h3 className="mt-3 text-[22px] font-bold tracking-tight">Analysis pipeline</h3>
      <p className="intro-mono mt-1 text-[12px] text-[#9b9ba3]">9 steps, rules first, model second</p>
      <ul className="mt-5 space-y-3.5">
        {STEPS.map(([n, name, note], i) => (
          <li key={n} className="intro-blur flex items-baseline gap-3" style={{ animationDelay: `${500 + i * 130}ms` }}>
            <span className="intro-mono w-5 text-[11px] text-[#b4b4bb]">{n}</span>
            <span className="text-[18px] font-semibold">{name}</span>
            <span className="intro-acc ml-auto text-[13px] font-medium">{note}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

/* ---------- scene 3: editor, a highlight sweeps down, headline blurs in ---------- */
const CODE = ['#include <vector>', 'using namespace std;', '', 'bool hasPairSum(const vector<int>& a, int k) {', '    for (size_t i = 0; i < a.size(); i++) {', '        for (size_t j = i + 1; j < a.size(); j++) {', '            if (a[i] + a[j] == k) return true;', '        }', '    }', '    return false;', '}'];
function Editor() {
  const [hl, setHl] = useState(-1);
  const [ph, setPh] = useState(0);
  useEffect(() => {
    const ts: number[] = [];
    for (let k = 0; k <= 6; k++) ts.push(window.setTimeout(() => setHl(k), 450 + k * 260));
    ts.push(window.setTimeout(() => setPh(1), 2300), window.setTimeout(() => setPh(2), 3300));
    return () => ts.forEach(window.clearTimeout);
  }, []);
  return (
    <div className="relative flex w-full items-center justify-center">
      <div className={`intro-card intro-pop ${ph >= 1 ? 'intro-dim' : ''}`} style={{ width: 'min(560px, 90vw)' }}>
        <div className="intro-mono flex gap-2 border-b border-[#eeeef2] px-4 py-2.5 text-[11px] text-[#8a8a92]"><b className="font-medium text-[#444]">main.cpp</b>C++17</div>
        <pre className="intro-mono m-0 py-3 text-[13px] leading-[1.75]">
          {CODE.map((l, i) => (
            <div key={i} className={`px-4 ${i === hl ? 'bg-[#f1f1f5]' : ''}`}><span className="intro-ln inline-block w-7">{i + 1}</span><Code line={l} /></div>
          ))}
        </pre>
      </div>
      {ph >= 1 && (
        <h2 className="intro-big absolute text-center">
          <Blur className={ph === 1 ? 'intro-acc' : ''}>Your code</Blur>
          {ph === 1 ? <Blur>.</Blur> : <>,&nbsp;<Blur delay={50} className="intro-grey">line by line.</Blur></>}
        </h2>
      )}
    </div>
  );
}

/* ---------- scene 4: headline left, tilted feature card right ---------- */
const FEATS: [LucideIcon, string, string, boolean][] = [
  [Timer, 'Complexity', 'Derived by rules', false],
  [Bug, 'Suspicious patterns', 'Reported by line', false],
  [FlaskConical, 'Counterexamples', 'Tested against a reference', false],
  [ShieldCheck, 'Optimization', 'Shown only after testing', true],
];
function Trust() {
  return (
    <div className="flex flex-col items-center gap-12 px-6 md:flex-row md:gap-24">
      <h2 className="intro-big text-center md:whitespace-nowrap md:text-left"><Blur delay={100}>Rules decide.</Blur><br /><Blur delay={750} className="intro-grey">The model advises.</Blur></h2>
      <div className="intro-card intro-tilt-r" style={{ width: 'min(400px, 88vw)', padding: 22 }}>
        <ul className="space-y-5">
          {FEATS.map(([Icon, t, d, hot], i) => (
            <li key={t} className="intro-blur flex items-center gap-4" style={{ animationDelay: `${500 + i * 160}ms` }}>
              <span className={`flex h-11 w-11 items-center justify-center rounded-xl ${hot ? 'bg-[#eceeff] text-[#4856e8]' : 'bg-[#f1f1f5] text-[#444]'}`}><Icon className="h-5 w-5" /></span>
              <span><b className="block text-[18px] leading-tight">{t}</b><span className="text-[14px] text-[#9b9ba3]">{d}</span></span>
            </li>
          ))}
        </ul>
      </div>
    </div>
  );
}

/* ---------- scene 5: camera pushes in, code types, run, result, tagline ---------- */
function Demo() {
  const [p, setP] = useState(0);
  useEffect(() => {
    const ts = [700, 2900, 3700, 5200].map((t, i) => window.setTimeout(() => setP(i + 1), t));
    return () => ts.forEach(window.clearTimeout);
  }, []);
  const typed = useTyped('for (int j = i + 1; j < n; j++) {', p >= 1, 42);
  const w = Math.min(460, window.innerWidth * 0.92);
  const s = Math.min(2.4, (window.innerWidth * 0.92) / w);
  const tf = p === 1 ? `translate(0px,0px) scale(${s})` : p === 2 ? `translate(${-w * 0.36 * s}px,${-18 * s}px) scale(${s})` : 'translate(0px,0px) scale(1)';
  return (
    <div className="flex flex-col items-center">
      <div className="intro-cam" style={{ transform: tf, width: w }}>
        <p className="intro-label">Pair sum</p>
        <div className="intro-card">
          <div className="flex items-center gap-3 px-5 pb-3 pt-4">
            <span className="intro-ln intro-mono text-[11px]">7</span>
            <span className="intro-mono text-[13px]">{p < 1 ? <span className="intro-grey">// your turn</span> : <Code line={typed} />}{p < 3 && <i className="intro-caret" />}</span>
          </div>
          <div className="flex items-center gap-3 px-5 pb-4 text-[11px] text-[#8a8a92]">
            <span>Language:</span><b className="intro-chip">C++17</b><span className="intro-dot" /><span>12 tests</span>
            <Undo2 className="ml-auto h-4 w-4" />
            <span className={`intro-run ${p === 2 ? 'intro-press' : ''}`}><Play className="h-3 w-3 fill-current" /></span>
          </div>
          <div className="intro-result" style={{ maxHeight: p >= 3 ? 80 : 0, opacity: p >= 3 ? 1 : 0 }}>
            <div className="flex items-center gap-3 text-[12px]"><b>Result</b><span className="intro-ok"><CheckCircle2 className="h-4 w-4" />Passed</span><span className="intro-mono text-[#8a8a92]">12/12 tests</span></div>
            <p className="mt-1 text-[11px] text-[#8a8a92]">Output matched the reference on every test.</p>
          </div>
        </div>
      </div>
      <div className="mt-10 min-h-[84px] text-center">
        {p >= 4 && <h2 className="intro-h2"><Words text="Optimized, then" /><Blur delay={450} className="intro-acc">tested.</Blur></h2>}
      </div>
    </div>
  );
}

const Tag = () => <h1 className="intro-big"><Words text="Analyze your code" step={110} /><Blur delay={420} className="intro-acc">.</Blur></h1>;
const Logo = () => (
  <div className="flex flex-col items-center gap-4">
    <Blur><LogoMark size={64} /></Blur>
    <Blur delay={150}><span className="intro-big !text-[3.4rem]">NeuroCode</span></Blur>
    <Blur delay={350}><span className="intro-mono text-[13px] tracking-wide text-[#9b9ba3]">analyze · optimize · test</span></Blur>
  </div>
);

const VIEWS: Record<(typeof SCENES)[number]['id'], () => JSX.Element> = { brand: Brand, pipeline: Pipeline, editor: Editor, trust: Trust, demo: Demo, tag: Tag, logo: Logo };

export default function Intro({ onDone }: { onDone: () => void }) {
  const [i, setI] = useState(0);
  const done = useRef(onDone);
  done.current = onDone;

  useEffect(() => {
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) { done.current(); return; }
    const t = window.setTimeout(() => (i + 1 >= SCENES.length ? done.current() : setI(i + 1)), SCENES[i].ms);
    return () => window.clearTimeout(t);
  }, [i]);

  const View = VIEWS[SCENES[i].id];
  return (
    <div className="intro fixed inset-0 z-50 overflow-hidden" role="presentation">
      <Scene key={SCENES[i].id} ms={SCENES[i].ms}><View /></Scene>
      <button onClick={() => done.current()} className="intro-skip">Skip intro</button>
      <div className="intro-progress" style={{ animationDuration: `${TOTAL}ms` }} />
    </div>
  );
}
