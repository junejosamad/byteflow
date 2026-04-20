"""
Phase 3.4 — ECO (Engineering Change Order) Flow Test

Validates:
  1. EcoEngine API exists and is callable
  2. fix_hold_violations inserts buffers on DFF chain (hold-violating paths)
  3. Hold WNS improves after buffer insertion
  4. fix_setup_violations upsizes gates when setup is violated
  5. run_timing_closure loop converges and EcoResult fields are correct
  6. Regression: designs without violations are not touched

Run from project root:
    python tests/test_eco.py
"""
import sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "build", "Release"))

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"
results = []

def check(name, condition, msg=""):
    status = PASS if condition else FAIL
    label  = f"  [{status}] {name}"
    if msg: label += f"  ({msg})"
    print(label)
    results.append((name, condition))
    return condition

def summarize():
    total  = len(results)
    passed = sum(1 for _, ok in results if ok)
    failed = total - passed
    print("\n" + "=" * 60)
    print(f"  Results: {passed}/{total} passed"
          + (f"  ({failed} FAILED)" if failed else ""))
    print("=" * 60)
    return failed == 0


BENCH_SEQ  = os.path.join(ROOT, "benchmarks/shift_reg.v")
BENCH_COMB = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")
PDK_LIB    = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib")
PDK_LEF    = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd_merged.lef")


def flow(verilog, pdk=False, period_ps=1000.0, uncertainty_ps=0.0):
    """Returns (chip, sta, spef) — caller must keep spef alive for Timer's lifetime."""
    import open_eda
    chip = open_eda.Design()
    if pdk:
        chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(verilog)
    open_eda.run_placement(chip)
    open_eda.RouteEngine().route(chip)
    spef = open_eda.SpefEngine()
    spef.extract(chip)
    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    sta.set_clock_period(period_ps)
    if uncertainty_ps:
        sta.set_clock_uncertainty(uncertainty_ps)
    sta.update_timing()
    return chip, sta, spef  # spef must outlive sta


def run_suite():
    print("=" * 60)
    print("  Phase 3.4 — ECO Timing Closure")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
        check("EcoEngine class exists",  hasattr(open_eda, "EcoEngine"))
        check("EcoResult class exists",  hasattr(open_eda, "EcoResult"))
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    # ── 1. API surface ─────────────────────────────────────────
    print("\n[Stage 1] EcoEngine API")
    eco = open_eda.EcoEngine()
    check("EcoEngine instantiates", eco is not None)
    check("fix_hold_violations method exists",
          hasattr(eco, "fix_hold_violations"))
    check("fix_setup_violations method exists",
          hasattr(eco, "fix_setup_violations"))
    check("run_timing_closure method exists",
          hasattr(eco, "run_timing_closure"))

    # ── 2. Hold fix on DFF chain ───────────────────────────────
    print("\n[Stage 2] fix_hold_violations — DFF chain")
    chip2, sta2, spef2 = flow(BENCH_SEQ)
    hold_before = sta2.get_hold_violation_count()
    hold_wns_before = sta2.get_hold_wns()
    check("shift_reg has hold violations before ECO",
          hold_before > 0,
          f"{hold_before} violations, wns={hold_wns_before:.1f}ps")

    n_inserted = eco.fix_hold_violations(chip2, sta2)
    check("buffers were inserted", n_inserted > 0,
          f"{n_inserted} buffers")

    # Rebuild STA after ECO netlist change
    sta2.build_graph()
    sta2.update_timing()
    hold_after = sta2.get_hold_violation_count()
    hold_wns_after = sta2.get_hold_wns()
    check("hold WNS improves after buffer insertion",
          hold_wns_after > hold_wns_before,
          f"before={hold_wns_before:.1f}ps  after={hold_wns_after:.1f}ps")

    # ── 3. Setup fix on sky130 combinational ───────────────────
    print("\n[Stage 3] fix_setup_violations — sky130 comb chain")
    # Force setup violations with a very tight clock (1 ps)
    chip3, sta3, spef3 = flow(BENCH_COMB, pdk=True, period_ps=1.0)
    setup_v_before = sta3.get_violation_count()
    wns_before = sta3.get_wns()
    check("sky130 chain has setup violations with 1 ps clock",
          setup_v_before > 0,
          f"{setup_v_before} violations, wns={wns_before:.1f}ps")

    n_resized = eco.fix_setup_violations(chip3, sta3)
    sta3.build_graph()
    sta3.update_timing()
    wns_after = sta3.get_wns()
    check("WNS improves (or gates resized) after setup ECO",
          n_resized > 0 or wns_after >= wns_before,
          f"resized={n_resized}  wns_before={wns_before:.1f}  wns_after={wns_after:.1f}")

    # ── 4. run_timing_closure — EcoResult fields ───────────────
    print("\n[Stage 4] run_timing_closure — EcoResult")
    chip4, sta4, spef4 = flow(BENCH_SEQ)
    result = eco.run_timing_closure(chip4, sta4, max_iter=5)

    check("EcoResult.iterations > 0",
          result.iterations > 0,
          f"{result.iterations}")
    check("EcoResult.final_setup_wns is finite",
          result.final_setup_wns not in (float("inf"), float("-inf")),
          f"{result.final_setup_wns:.1f}ps")
    check("EcoResult.final_hold_wns is finite",
          result.final_hold_wns not in (float("inf"), float("-inf")),
          f"{result.final_hold_wns:.1f}ps")
    check("EcoResult.final_setup_viols >= 0",
          result.final_setup_viols >= 0,
          f"{result.final_setup_viols}")
    check("hold WNS improved vs no ECO",
          result.final_hold_wns > hold_wns_before,
          f"eco={result.final_hold_wns:.1f}ps  no_eco={hold_wns_before:.1f}ps")

    # ── 5. Clean design: ECO makes no changes ─────────────────
    print("\n[Stage 5] Clean design — ECO is a no-op")
    chip5, sta5, spef5 = flow(BENCH_COMB, pdk=True, period_ps=2000.0)
    v5 = sta5.get_violation_count()
    hv5 = sta5.get_hold_violation_count()
    check("comb design has no violations (baseline)",
          v5 == 0 and hv5 == 0,
          f"setup={v5}  hold={hv5}")

    n_s = eco.fix_setup_violations(chip5, sta5)
    n_h = eco.fix_hold_violations(chip5, sta5)
    check("no ECO changes on clean design",
          n_s == 0 and n_h == 0,
          f"resized={n_s}  inserted={n_h}")

    # ── 6. Regression: full suites unaffected ─────────────────
    print("\n[Stage 6] Regression")
    for bench, label in [("benchmarks/full_adder.v", "full_adder"),
                          ("benchmarks/adder.v",      "adder")]:
        path = os.path.join(ROOT, bench)
        if not os.path.exists(path): continue
        chip_r = open_eda.Design()
        chip_r.load_verilog(path)
        open_eda.run_placement(chip_r)
        open_eda.RouteEngine().route(chip_r)
        spef_r = open_eda.SpefEngine(); spef_r.extract(chip_r)
        sta_r = open_eda.Timer(chip_r, spef_r)
        sta_r.build_graph(); sta_r.set_clock_period(1000.0); sta_r.update_timing()
        check(f"{label}: WNS finite after Phase 3.4",
              sta_r.get_wns() not in (float("inf"), float("-inf")),
              f"WNS={sta_r.get_wns():.1f}ps")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
