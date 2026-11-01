#!/usr/bin/env python3
"""End to end check of the contest-code path: strip_io() in tools/build_dataset.py versus the C++ IoStripper,
then the 53 features of the stripped code, Python pipeline versus C++ pipeline.

For every .cpp under tests/fixtures/strip_samples (put real contest solutions there for a better answer):
  1. stripped TEXT: build_dataset.strip_io(src) without its preamble must equal `nc_strip` output exactly
  2. FEATURES: extract_ast + features.py on build_dataset's text (its own preamble, as in training) versus
     `nc_extract --strip --features` (C++ stripper, parse preamble of ClangFrontend, C++ FeatureExtractor)

Run from backend/:
  python3 tests/parity_strip.py --nc-strip build/nc_strip --nc-extract build/nc_extract \
      --dataset-script tools/build_dataset.py [--tools tools] [--samples tests/fixtures/strip_samples]
Set NC_LIBCLANG=/path/to/libclang.so so Python and C++ share one libclang.
"""
import argparse, glob, importlib.util, json, os, subprocess, sys, tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nc-strip", required=True)
    ap.add_argument("--nc-extract", required=True)
    ap.add_argument("--dataset-script", required=True)
    ap.add_argument("--tools", default="tools")
    ap.add_argument("--samples", default="tests/fixtures/strip_samples")
    args = ap.parse_args()

    sys.path.insert(0, args.tools)
    import clang.cindex as ci
    if os.environ.get("NC_LIBCLANG"):
        ci.Config.set_library_file(os.environ["NC_LIBCLANG"])
    import extract_ast, features
    spec = importlib.util.spec_from_file_location("build_dataset_under_test", args.dataset_script)
    ds = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(os.path.abspath(args.dataset_script)))
    spec.loader.exec_module(ds)

    files = sorted(glob.glob(os.path.join(args.samples, "*.cpp")))
    if not files:
        sys.exit(f"no samples in {args.samples}")

    bad = 0
    for path in files:
        name = os.path.basename(path)
        src = open(path, errors="ignore").read()
        py_full = ds.strip_io(src)                              # preamble + stripped code
        py_text = py_full[len(ds.PREAMBLE):]
        cpp_text = subprocess.run([args.nc_strip, path], capture_output=True, text=True, check=True).stdout
        problems = []
        if py_text != cpp_text:
            problems.append("stripped text differs")

        with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
            f.write(py_full)
            tmp = f.name
        try:
            py_ast, py_errs = extract_ast.parse_file(tmp)
        finally:
            os.unlink(tmp)
        v_py = features.extract(py_ast)
        out = subprocess.run([args.nc_extract, "--strip", "--features", path], capture_output=True, text=True)
        if out.returncode != 0:
            problems.append(f"nc_extract exited {out.returncode}: {out.stderr.strip()[:150]}")
        else:
            v_cpp = json.loads(out.stdout)
            for i, (a, b) in enumerate(zip(v_py, v_cpp)):
                if a != b:
                    problems.append(f"feature {features.FEATURE_NAMES[i]}: python={a} c++={b}")
        if problems:
            bad += 1
            print(f"FAIL {name}:")
            for p in problems[:8]:
                print("    " + p)
        else:
            note = "" if not py_errs else f"  (both sides parse it with {len(py_errs)} errors, as the training pipeline would drop it)"
            print(f"ok   {name}: stripped text and 53 features identical{note}")

    print("ALL IDENTICAL" if not bad else f"{bad} of {len(files)} files differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
