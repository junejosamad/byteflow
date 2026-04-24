"""
OpenEDA test runner — executes every test_*.py in tests/ and reports overall pass/fail.
Exit code: 0 if all suites pass, 1 if any suite fails.

Usage:
    python run_all_tests.py
    python run_all_tests.py --stop-on-fail   # stop after first failing suite
"""
import sys, os, subprocess, argparse, time, copy
# Force UTF-8 output on Windows (handles unicode box-drawing chars in test output)
if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.abspath(__file__))
TESTS_DIR = os.path.join(ROOT, "tests")

def find_test_files():
    files = sorted(
        f for f in os.listdir(TESTS_DIR)
        if f.startswith("test_") and f.endswith(".py")
    )
    return [os.path.join(TESTS_DIR, f) for f in files]

def run_test(path):
    env = copy.copy(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    t0 = time.time()
    result = subprocess.run(
        [sys.executable, path],
        capture_output=False,
        cwd=ROOT,
        env=env,
    )
    elapsed = time.time() - t0
    return result.returncode == 0, elapsed

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stop-on-fail", action="store_true")
    args = parser.parse_args()

    test_files = find_test_files()
    if not test_files:
        print("No test files found in", TESTS_DIR)
        sys.exit(1)

    print("=" * 70)
    print(f"  OpenEDA Test Runner — {len(test_files)} suite(s)")
    print("=" * 70)

    passed, failed = [], []
    for path in test_files:
        name = os.path.basename(path)
        print(f"\n{'─' * 70}")
        print(f"  Running: {name}")
        print(f"{'─' * 70}")
        ok, elapsed = run_test(path)
        if ok:
            passed.append((name, elapsed))
            print(f"\n  >>> {name}: PASS  ({elapsed:.1f}s)")
        else:
            failed.append((name, elapsed))
            print(f"\n  >>> {name}: FAIL  ({elapsed:.1f}s)")
            if args.stop_on_fail:
                break

    total = len(passed) + len(failed)
    print("\n" + "=" * 70)
    print(f"  Summary: {len(passed)}/{total} suites passed")
    if failed:
        print(f"  FAILED:")
        for name, elapsed in failed:
            print(f"    - {name}  ({elapsed:.1f}s)")
    print("=" * 70)

    sys.exit(0 if not failed else 1)

if __name__ == "__main__":
    main()
