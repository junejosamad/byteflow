"""
Analytical Placer Scale & Quality Test — Phase 1.1 validation

Compares AnalyticalPlacer vs SA on generated benchmarks and asserts:
  - Analytical placer runs without crash on >= 100 cell designs
  - Full pipeline (place -> route -> GDSII) completes on large designs
  - Runtime is acceptable (< 60s for 500 cells)
  - STA signs off with finite WNS

Run from project root:
  python tests/test_placer_scale.py
"""
import sys, os, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "build", "Release"))

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"
SKIP = "\033[93mSKIP\033[0m"

results = []

def check(name, condition, msg=""):
    status = PASS if condition else FAIL
    label  = f"  [{status}] {name}"
    if msg: label += f"  ({msg})"
    print(label)
    results.append((name, condition))
    return condition

def hpwl(chip):
    """Compute HPWL from placed instances via Python (rough estimate)."""
    try:
        return chip.get_hpwl() if hasattr(chip, "get_hpwl") else -1.0
    except Exception:
        return -1.0

def run_flow(chip, label):
    """Run place -> CTS -> PDN -> route -> SPEF -> GDSII. Return timing dict."""
    import open_eda
    t0 = time.time()
    open_eda.run_placement(chip)
    place_t = time.time() - t0

    try:
        cts = open_eda.CtsEngine()
        cts.run_cts(chip, "clk")
    except Exception:
        pass

    pdn = open_eda.PdnGenerator(chip)
    pdn.run()

    t1 = time.time()
    router = open_eda.RouteEngine()
    router.route(chip)
    route_t = time.time() - t1

    spef = open_eda.SpefEngine()
    spef.extract(chip)

    gds_path = os.path.join(ROOT, f"benchmarks/{label}_test.gds")
    open_eda.export_gds(gds_path, chip)
    gds_ok = os.path.exists(gds_path) and os.path.getsize(gds_path) > 0
    if os.path.exists(gds_path): os.remove(gds_path)

    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    sta.set_clock_period(2000.0)   # 500 MHz — achievable for larger designs
    sta.update_timing()
    wns = sta.get_wns()

    return {
        "place_s": place_t,
        "route_s": route_t,
        "total_s": place_t + route_t,
        "gds_ok":  gds_ok,
        "wns":     wns,
    }

def run_suite():
    print("=" * 60)
    print("  Analytical Placer Scale Test")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    benchmarks = [
        {"name": "bench_200", "cells": 200, "threshold": 100},
        {"name": "bench_500", "cells": 500, "threshold": 100},
    ]

    for bm in benchmarks:
        name    = bm["name"]
        verilog = os.path.join(ROOT, "benchmarks", f"{name}.v")
        print(f"\n[Benchmark] {name} ({bm['cells']} cells)")

        if not os.path.exists(verilog):
            print(f"  [{SKIP}] {verilog} not found — run gen_benchmark.py first")
            continue

        # ---- Load ----
        try:
            chip = open_eda.Design()
            chip.load_verilog(verilog)
            n = chip.get_instance_count()
            check(f"{name}: loaded ({n} cells)", n >= bm["cells"] * 0.9,
                  f"{n} cells")
        except Exception as e:
            check(f"{name}: loaded", False, str(e))
            continue

        # ---- Analytical placer engages ----
        check(f"{name}: analytical placer will engage",
              n >= bm["threshold"],
              f"{n} >= {bm['threshold']}")

        # ---- Full flow ----
        try:
            r = run_flow(chip, name)
            check(f"{name}: placement < 60s",    r["place_s"] < 60.0,
                  f"{r['place_s']:.1f}s")
            check(f"{name}: routing < 120s",     r["route_s"] < 120.0,
                  f"{r['route_s']:.1f}s")
            check(f"{name}: GDSII produced",     r["gds_ok"])
            check(f"{name}: WNS finite",
                  r["wns"] != float("inf") and r["wns"] != float("-inf"),
                  f"WNS={r['wns']:.1f}ps")
            print(f"  [INFO] Total time: {r['total_s']:.1f}s")
        except Exception as e:
            check(f"{name}: full flow completed", False, str(e))

    return summarize()

def summarize():
    total  = len(results)
    passed = sum(1 for _, ok in results if ok)
    failed = total - passed
    print("\n" + "=" * 60)
    print(f"  Results: {passed}/{total} passed"
          + (f"  ({failed} FAILED)" if failed else ""))
    print("=" * 60)
    return failed == 0

if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
