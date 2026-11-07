import { useState, type FormEvent } from 'react';
import { useAuth } from '../hooks/useAuth';
import { Button, ErrorBox, Segmented } from '../components/common/ui';
import { LogoMark } from '../components/layout/Logo';
import { errorMessage } from '../lib/errors';
import OptimizationsChart from '../components/landing/OptimizationsChart';

export default function Login({ onReplay }: { onReplay?: () => void }) {
  const { login, signup } = useAuth();
  const [mode, setMode] = useState<'login' | 'signup'>('login');
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setErr(''); setBusy(true);
    try { await (mode === 'login' ? login : signup)(username.trim(), password); } catch (x) { setErr(errorMessage(x)); } finally { setBusy(false); }
  };

  return (
    <div className="grid min-h-screen lg:grid-cols-[1.1fr_1fr]">
      <div className="hidden flex-col justify-between border-r border-line bg-panel p-12 lg:flex">
        <div className="flex items-center gap-3"><LogoMark size={36} /><span className="font-display text-2xl font-bold">NeuroCode</span></div>
        <div>
          <h1 className="max-w-lg text-4xl font-bold leading-[1.12]">C++ code reasoning and optimization</h1>
          <p className="mt-4 max-w-md text-mute">Submit a C++ solution and a short description of the problem it solves. NeuroCode analyzes it and, when a better approach exists, produces an optimized version.</p>
          <ul className="mt-8 max-w-md space-y-2.5 text-sm">
            {['Parses the code into an AST and builds a control flow graph', 'Derives time and space complexity by rules', 'Classifies the algorithm with a trained model, as a ranked list', 'Runs the code in a sandbox against generated tests', 'Offers an optimized version with a measured speedup'].map((t) => (
              <li key={t} className="flex gap-3"><span className="mt-2 h-1.5 w-1.5 shrink-0 rounded-full bg-accent" /><span>{t}</span></li>
            ))}
          </ul>
          <div className="mt-8 max-w-md">
            <OptimizationsChart />
          </div>
        </div>
        <p className="max-w-md text-sm text-mute">Testing finds counterexamples. It does not prove that code is correct.</p>
      </div>
      <div className="flex items-center justify-center p-8">
        <form onSubmit={submit} className="w-full max-w-sm space-y-5">
          <div><h2 className="text-3xl font-bold">{mode === 'login' ? 'Sign in' : 'Create your account'}</h2>
            <p className="mt-1 text-mute">{mode === 'login' ? 'Pick up where you left off.' : 'Your analyses are saved to your account.'}</p></div>
          <Segmented value={mode} onChange={setMode} label="Account mode" options={[{ value: 'login', label: 'Sign in' }, { value: 'signup', label: 'Sign up' }]} />
          {err && <ErrorBox title={mode === 'login' ? 'Could not sign in' : 'Could not create the account'}>{err}</ErrorBox>}
          <div><label htmlFor="u" className="label">Username</label><input id="u" className="field" autoComplete="username" required minLength={3} value={username} onChange={(e) => setUsername(e.target.value)} /></div>
          <div><label htmlFor="p" className="label">Password</label><input id="p" type="password" className="field" autoComplete={mode === 'login' ? 'current-password' : 'new-password'} required minLength={8} value={password} onChange={(e) => setPassword(e.target.value)} />
            {mode === 'signup' && <p className="hint">At least 8 characters.</p>}</div>
          <Button type="submit" size="lg" className="w-full" loading={busy}>{mode === 'login' ? 'Sign in' : 'Create account'}</Button>
          {onReplay && <button type="button" onClick={onReplay} className="w-full text-center text-sm text-mute hover:text-ink">Replay intro</button>}
        </form>
      </div>
    </div>
  );
}