#!/usr/bin/env python3
"""Writes tests/fixtures/strip_golden.json: what tools/build_dataset.py's strip_io() produces for the
sample programs and for a list of tricky snippets. tests/test_io_stripper.cpp compares the C++ IoStripper
against it, so the C++ side can be tested without Python. Regenerate only when build_dataset.py changes.

Run from backend/:  python3 tests/gen_strip_golden.py --dataset-script tools/build_dataset.py
"""
import argparse, glob, importlib.util, json, os, sys

SNIPPETS = {
    "cin_cout_basic": "int main(){ int n; cin >> n; cout << n << endl; return 0; }",
    "input_loop_removed": "int main(){ int n, a[5]; cin >> n; for (int i = 0; i < n; i++) cin >> a[i]; return 0; }",
    "while_cin_swallows_body": "int main(){ int x, s = 0; while (cin >> x) { s += x; } return s; }",
    "std_prefix": "int main(){ std::cout << 1 << std::endl; std::cin >> x; }",
    "string_with_semicolon": "int main(){ cout << \"a;b\" << x; printf(\"%d;\\n\", x); return 0; }",
    "char_literal_semicolon": "int main(){ cout << ';'; return 0; }",
    "multiline_cout": "int main(){\n  cout << \"x\"\n       << y\n       << endl;\n  return 0;\n}",
    "sync_and_tie": "int main(){ ios_base::sync_with_stdio(false); cin.tie(nullptr); cout.tie(0); }",
    "std_sync_leaves_prefix": "int main(){ std::ios::sync_with_stdio(false); }",
    "tie_chain_not_matched": "int main(){ cin.tie(0)->sync_with_stdio(0); }",
    "fprintf_not_matched": "int main(){ fprintf(stderr, \"x\"); sprintf(buf, \"%d\", n); }",
    "getline": "int main(){ string s; getline(cin, s); getline(ss, t, ','); }",
    "do_while_loses_tail": "int f(int x){ do { x++; } while (x < 3); return x; }",
    "originally_empty_loops": "int f(int x){ while (x--); for (;;); return x; }",
    "nested_paren_loop_header": "int f(){ for (int i = g(h(1)); i < n; i++) cin >> a[i]; return 0; }",
    "includes": "#include <bits/stdc++.h>\n  #  include <vector>\n#define N 5\nusing namespace std;\nint f(){ return N; }\n",
    "word_boundaries": "int main(){ mycout << 1; cinema >> x; myprintf(1); return 0; }",
    "no_io_unchanged": "int f(vector<int>& a){ int s = 0; for (int i = 0; i < (int)a.size(); i++) s += a[i]; return s; }",
    "crlf_and_blank_lines": "int a;\r\n\r\nint main(){ cin >> a; }\r\n",
    "comment_with_io": "// cout << 1;\nint f(){ /* printf(\"x\"); */ return 1; }\n",
    "empty": "",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset-script", required=True)
    ap.add_argument("--tools", default="tools")
    ap.add_argument("--samples", default="tests/fixtures/strip_samples")
    ap.add_argument("--out", default="tests/fixtures/strip_golden.json")
    args = ap.parse_args()

    sys.path.insert(0, args.tools)
    spec = importlib.util.spec_from_file_location("build_dataset_under_test", args.dataset_script)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(os.path.abspath(args.dataset_script)))
    spec.loader.exec_module(mod)
    pre = mod.PREAMBLE

    cases = []
    for path in sorted(glob.glob(os.path.join(args.samples, "*.cpp"))):
        src = open(path, errors="ignore").read()
        cases.append({"name": os.path.basename(path), "input": src, "expected": mod.strip_io(src)[len(pre):]})
    for name, src in SNIPPETS.items():
        cases.append({"name": name, "input": src, "expected": mod.strip_io(src)[len(pre):]})
    with open(args.out, "w") as f:
        json.dump({"preamble": pre, "cases": cases}, f, indent=1)
    print(f"wrote {len(cases)} cases to {args.out}")


if __name__ == "__main__":
    main()
