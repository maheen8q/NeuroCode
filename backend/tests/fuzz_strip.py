#!/usr/bin/env python3
"""Random-input comparison of the C++ IoStripper against strip_io() in tools/build_dataset.py.

Builds N random "programs" out of code fragments, I/O statements, quirky cases and noise characters, strips
each with both implementations and requires identical text. A failure prints the smallest differing input.

Run from backend/:
  python3 tests/fuzz_strip.py --nc-strip build/nc_strip --dataset-script tools/build_dataset.py [-n 5000] [--seed 1]
"""
import argparse, importlib.util, json, os, random, subprocess, sys

FRAGMENTS = [
    "#include <bits/stdc++.h>\n", "  #  include <vector>\n", "#define N 100\n", "using namespace std;\n",
    "int main() {\n", "}\n", "{", "}", ";", ";\n", "\n", " ", "\t",
    "cin >> n;", "cin>>a[i]>>b;", "cout << x << endl;", "cout<<\"a;b\"<<x;", "std::cout << std::endl;", "cerr << 1;",
    "cout << ';';", "cout << \"x\\\"y;z\";", "cout << \"unterminated;\n", "printf(\"%d\\n\", x);", "scanf(\"%d\", &n);",
    "puts(\"hi\");", "freopen(\"in.txt\", \"r\", stdin);", "fprintf(stderr, \"x\");", "sprintf(buf, \"%d\", n);",
    "ios_base::sync_with_stdio(false);", "std::ios::sync_with_stdio(0);", "cin.tie(nullptr);", "cin.tie(0)->sync_with_stdio(0);",
    "cout.tie(0);", "getline(cin, s);", "getline(ss, t, ',');", "string getlinex;", "mycout << 1;", "cinema >> x;",
    "for (int i = 0; i < n; i++) cin >> a[i];", "for (int i = 0; i < n; i++) ;", "for(;;);", "while (t--) {", "while (x--);",
    "while (cin >> x) { s += x; }", "do { x++; } while (x < 3);", "for (int i = 0; i < (int)v.size(); i++) { v[i]++; }",
    "for (auto it = m.begin(); it != m.end(); ++it) cout << it->first;", "while (f(g(1), 2)) ;", "while (f(g(h(1)))) ;",
    "int a[100], n, k;", "vector<int> v(n);", "sort(v.begin(), v.end());", "x = a << 2; y = b >> 1;", "if (a == b) x++; else y--;",
    "\"cin >> x;\"", "// cout << 1;\n", "/* printf(\"x\"); */", "'\"'", "\"\\\\\"", "(", ")", "((", "))", "\\\n", "\r\n", "\r", "é", "\x0b", "\x0c",
]
NOISE = list("abcdefgxyz01_<>()[]{};:\"'\\ \n\t#.,=+-*/&|!?")


def make_case(rng):
    parts = []
    for _ in range(rng.randint(1, 18)):
        if rng.random() < 0.8:
            parts.append(rng.choice(FRAGMENTS))
        else:
            parts.append("".join(rng.choice(NOISE) for _ in range(rng.randint(1, 8))))
        if rng.random() < 0.3:
            parts.append(rng.choice([" ", "\n", ""]))
    return "".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nc-strip", required=True)
    ap.add_argument("--dataset-script", required=True)
    ap.add_argument("--tools", default="tools")
    ap.add_argument("-n", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    sys.path.insert(0, args.tools)
    spec = importlib.util.spec_from_file_location("build_dataset_under_test", args.dataset_script)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(os.path.abspath(args.dataset_script)))
    spec.loader.exec_module(mod)
    preamble = mod.PREAMBLE

    rng = random.Random(args.seed)
    cases = [make_case(rng) for _ in range(args.n)]
    out = subprocess.run([args.nc_strip, "--json"], input=json.dumps(cases), capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"nc_strip failed: {out.stderr[:300]}")
    cpp = json.loads(out.stdout)
    bad = [(c, mod.strip_io(c)[len(preamble):], got) for c, got in zip(cases, cpp) if mod.strip_io(c)[len(preamble):] != got]
    if not bad:
        print(f"ok: {len(cases)} random inputs, C++ and Python strip identically")
        return 0
    c, want, got = min(bad, key=lambda b: len(b[0]))
    print(f"FAIL: {len(bad)} of {len(cases)} differ. Smallest input:\n{c!r}\npython: {want!r}\nc++:    {got!r}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
