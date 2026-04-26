"""
Phase 3.1 — Multi-Corner Multi-Mode (MCMM) STA Test

Validates:
  1. addCorner() registers slow/typical/fast corners from the same Liberty file
  2. runAllCorners() runs STA independently per corner (no cross-contamination)
  3. WNS ordering: slow (tight clock) <= typical <= fast (relaxed clock)
  4. getWorstCorner() identifies the corner with minimum WNS
  5. formatMcmmReport() produces a non-empty ASCII table with all corner names
  6. Tight-clock corner (100ps) shows setup violations
  7. Relaxed-clock corner (10000ps) meets timing
  8. Individual corner retrieval via getCornerResult()
  9. CornerResult fields (corner_name, mode_name, endpoints) are populated
 10. runAllCorners() with no corners registered warns but does not crash

Run from project root:
    python tests/test_mcmm_sta.py
"""
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "build", "Release"))

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"
results = []


def check(name, condition, msg=""):
    status = PASS if condition else FAIL
    label = f"  [{status}] {name}"
    if msg:
        label += f"  ({msg})"
    print(label)
    results.append((name, condition))
    return condition


def summarize():
    total = len(results)
    passed = sum(1 for _, ok in results if ok)
    failed = total - passed
    print("\n" + "=" * 60)
    print(f"  Results: {passed}/{total} passed"
          + (f"  ({failed} FAILED)" if failed else ""))
    print("=" * 60)
    return failed == 0


PDK_LIB = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib")
PDK_LEF = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd_merged.lef")
BENCH_V = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")


def load_design():
    import open_eda
    chip = open_eda.Design()
    chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(BENCH_V)
    open_eda.run_placement(chip)
    router = open_eda.RouteEngine()
    router.route(chip)
    return chip


def run_suite():
    print("=" * 60)
    print("  Phase 3.1 — MCMM STA Test")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    for label, path in [("sky130 Liberty", PDK_LIB),
                        ("sky130 LEF",     PDK_LEF),
                        ("benchmark .v",   BENCH_V)]:
        if not check(f"{label} file exists", os.path.exists(path), path):
            return summarize()

    # ── Load design once ──────────────────────────────────────
    print("\n[Setup] Loading design...")
    try:
        chip = load_design()
        check("load_design", True)
    except Exception as e:
        check("load_design", False, str(e))
        return summarize()

    spef = open_eda.SpefEngine()
    spef.extract(chip)
    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    check("build_graph", True)

    # ── Stage 1: Register three corners ───────────────────────
    print("\n[Stage 1] Register slow / typical / fast corners")
    # All use the same Liberty file but different clock periods.
    # The sky130_inv_chain benchmark critical-path AT is ~10ps, so:
    #   slow    = 5ps   -> tighter than critical path, forces setup violations
    #   typical = 100ps -> comfortable margin (WNS ~90ps)
    #   fast    = 10000ps -> extremely relaxed
    slow_ok = sta.add_corner("slow",    PDK_LIB, period_ps=5.0)
    typ_ok  = sta.add_corner("typical", PDK_LIB, period_ps=100.0)
    fast_ok = sta.add_corner("fast",    PDK_LIB, period_ps=10000.0)
    check("add_corner slow",    slow_ok)
    check("add_corner typical", typ_ok)
    check("add_corner fast",    fast_ok)

    # ── Stage 2: run_all_corners ──────────────────────────────
    print("\n[Stage 2] run_all_corners")
    try:
        sta.run_all_corners()
        check("run_all_corners no exception", True)
    except Exception as e:
        check("run_all_corners no exception", False, str(e))
        return summarize()

    all_results = sta.get_all_corner_results()
    check("get_all_corner_results returns 3", len(all_results) == 3,
          f"got {len(all_results)}")

    # ── Stage 3: WNS ordering (slower clock -> worse WNS) ──────
    print("\n[Stage 3] WNS ordering: slow <= typical <= fast")
    slow_r = sta.get_corner_result("slow")
    typ_r  = sta.get_corner_result("typical")
    fast_r = sta.get_corner_result("fast")
    check("slow WNS <= typical WNS", slow_r.wns <= typ_r.wns,
          f"slow={slow_r.wns:.1f}ps  typ={typ_r.wns:.1f}ps")
    check("typical WNS <= fast WNS", typ_r.wns <= fast_r.wns,
          f"typ={typ_r.wns:.1f}ps  fast={fast_r.wns:.1f}ps")

    # ── Stage 4: getWorstCorner identifies slow ───────────────
    print("\n[Stage 4] Worst corner identification")
    worst = sta.get_worst_corner()
    check("worst corner_name == 'slow'", worst.corner_name == "slow",
          f"got '{worst.corner_name}'")
    check("worst WNS matches slow WNS",
          abs(worst.wns - slow_r.wns) < 0.1,
          f"worst={worst.wns:.1f}  slow={slow_r.wns:.1f}")

    # ── Stage 5: formatMcmmReport content ────────────────────
    print("\n[Stage 5] MCMM report format")
    report = sta.format_mcmm_report()
    check("format_mcmm_report non-empty",   len(report) > 20)
    check("report contains 'slow'",    "slow"    in report)
    check("report contains 'typical'", "typical" in report)
    check("report contains 'fast'",    "fast"    in report)
    check("report contains 'WORST'",   "WORST"   in report)

    # ── Stage 6: tight clock forces setup violations ──────────
    print("\n[Stage 6] Tight clock -> setup violations")
    check("slow corner has violations",
          slow_r.violations > 0,
          f"violations={slow_r.violations}")

    # ── Stage 7: relaxed clock meets timing ───────────────────
    print("\n[Stage 7] Relaxed clock -> timing met")
    check("fast corner violations == 0",
          fast_r.violations == 0,
          f"violations={fast_r.violations}")

    # ── Stage 8: individual result retrieval fields ───────────
    print("\n[Stage 8] CornerResult field access")
    check("slow corner_name field",  slow_r.corner_name == "slow")
    check("fast mode_name field",    fast_r.mode_name   == "functional")
    check("typical endpoints > 0",   typ_r.endpoints    > 0,
          f"endpoints={typ_r.endpoints}")
    check("slow TNS <= 0",           slow_r.tns <= 0.0,
          f"tns={slow_r.tns:.1f}ps")
    check("fast TNS == 0",           fast_r.tns == 0.0,
          f"tns={fast_r.tns:.1f}ps")

    # ── Stage 9: missing corner returns empty result ──────────
    print("\n[Stage 9] Missing corner result")
    missing = sta.get_corner_result("nonexistent")
    check("missing corner WNS == 0", missing.wns == 0.0)
    check("missing corner name empty", missing.corner_name == "")

    # ── Stage 10: no corners -> warns but does not crash ───────
    print("\n[Stage 10] Empty corner set is safe")
    try:
        sta2 = open_eda.Timer(chip, spef)
        sta2.build_graph()
        sta2.run_all_corners()
        empty = sta2.get_all_corner_results()
        check("no-corner run_all_corners safe", True)
        check("no-corner results list empty",   len(empty) == 0,
              f"got {len(empty)}")
        worst2 = sta2.get_worst_corner()
        check("no-corner worst WNS == 0", worst2.wns == 0.0)
    except Exception as e:
        check("no-corner run_all_corners safe", False, str(e))

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
