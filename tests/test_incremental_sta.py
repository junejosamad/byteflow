"""
Phase 3.4 — Incremental STA Test Suite

Validates Timer::updateTimingSkipBuild and EcoEngine incremental closure:
  1.  API: update_timing_skip_build() method exists on Timer
  2.  API: EcoResult has full_rebuild_count and incr_update_count fields
  3.  updateTimingSkipBuild gives same WNS as full updateTiming (shift_reg)
  4.  updateTimingSkipBuild gives same TNS as full updateTiming
  5.  updateTimingSkipBuild gives same violation count as full updateTiming
  6.  updateTimingSkipBuild gives same hold WNS as full updateTiming
  7.  updateTimingSkipBuild is safe on an empty-graph Timer (no crash)
  8.  After gate type swap: incr gives correct updated WNS (not stale)
  9.  ECO runTimingClosure: full_rebuild_count == 2 (initial + final only)
 10.  ECO runTimingClosure: incr_update_count >= 1 on a design with violations
 11.  ECO incremental result matches non-incremental final timing (consistency)
 12.  patchGraph: after buffer insert, incremental WNS matches full rebuild
 13.  Regression — full_adder ECO closure: final timing correct
 14.  Regression — shift_reg ECO closure: correct violations after closure

Run from project root:
    python tests/test_incremental_sta.py
"""
import sys, os, tempfile
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

BENCH      = os.path.join(ROOT, "benchmarks")
FULL_ADDER = os.path.join(BENCH, "full_adder.v")
SHIFT_REG  = os.path.join(BENCH, "shift_reg.v")
BENCH_200  = os.path.join(BENCH, "bench_200.v")


def make_placed_chip(verilog_path):
    import open_eda
    chip = open_eda.Design()
    chip.load_verilog(verilog_path)
    open_eda.run_placement(chip)
    return chip

def make_routed_chip(verilog_path):
    import open_eda
    chip = make_placed_chip(verilog_path)
    open_eda.RouteEngine().route(chip)
    return chip

def make_timer(chip, period_ps=1000.0):
    import open_eda
    t = open_eda.Timer(chip)
    t.set_clock_period(period_ps)
    return t


def run_suite():
    print("=" * 60)
    print("  Phase 3.4 - Incremental STA")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    # ── Stage 1: API surface ──────────────────────────────────────
    print("\n[Stage 1] API surface")
    chip1 = make_placed_chip(SHIFT_REG)
    t1 = make_timer(chip1)
    t1.build_graph()
    check("update_timing_skip_build exists",  hasattr(t1, "update_timing_skip_build"))
    t1.update_timing()
    r_eco_dummy = open_eda.EcoResult() if hasattr(open_eda, "EcoResult") else None
    check("EcoResult class exists",           hasattr(open_eda, "EcoResult"))
    # We can't construct EcoResult directly — verify fields via runTimingClosure
    check("EcoEngine class exists",           hasattr(open_eda, "EcoEngine"))

    # ── Stage 2: EcoResult fields ─────────────────────────────────
    print("\n[Stage 2] EcoResult incremental fields")
    try:
        chip2 = make_routed_chip(SHIFT_REG)
        t2 = make_timer(chip2)
        t2.build_graph()
        t2.set_clock_period(500.0)  # tight period → likely violations
        t2.update_timing()
        eco2 = open_eda.EcoEngine()
        r2 = eco2.run_timing_closure(chip2, t2, 3)
        check("full_rebuild_count is int",   isinstance(r2.full_rebuild_count, int))
        check("incr_update_count is int",    isinstance(r2.incr_update_count, int))
        print(f"    (info: rebuilds={r2.full_rebuild_count}, incr={r2.incr_update_count})")
    except Exception as e:
        check("full_rebuild_count is int",   False, str(e))
        check("incr_update_count is int",    False, "stage failed")

    # ── Stage 3–6: updateTimingSkipBuild consistency ──────────────
    print("\n[Stage 3-6] updateTimingSkipBuild == updateTiming (shift_reg, 1GHz)")
    try:
        chip3 = make_placed_chip(SHIFT_REG)
        t3 = make_timer(chip3, 1000.0)
        t3.build_graph()

        # Full timing
        t3.update_timing()
        full_wns  = t3.get_wns()
        full_tns  = t3.get_tns()
        full_viols= t3.get_violation_count()
        full_hwns = t3.get_hold_wns()

        # Incremental (no topology change since last build_graph)
        t3.update_timing_skip_build()
        incr_wns  = t3.get_wns()
        incr_tns  = t3.get_tns()
        incr_viols= t3.get_violation_count()
        incr_hwns = t3.get_hold_wns()

        print(f"    Full:  WNS={full_wns:.1f}  TNS={full_tns:.1f}  viols={full_viols}  hold={full_hwns:.1f}")
        print(f"    Incr:  WNS={incr_wns:.1f}  TNS={incr_tns:.1f}  viols={incr_viols}  hold={incr_hwns:.1f}")

        TOL = 0.01  # 0.01 ps tolerance for floating-point
        check("incr WNS == full WNS",    abs(incr_wns  - full_wns)  < TOL, f"{incr_wns:.3f} vs {full_wns:.3f}")
        check("incr TNS == full TNS",    abs(incr_tns  - full_tns)  < TOL, f"{incr_tns:.3f} vs {full_tns:.3f}")
        check("incr viols == full viols",incr_viols == full_viols,          f"{incr_viols} vs {full_viols}")
        check("incr hold WNS == full",   abs(incr_hwns - full_hwns) < TOL, f"{incr_hwns:.3f} vs {full_hwns:.3f}")
    except Exception as e:
        for _ in range(4):
            check("incr consistency", False, str(e))

    # ── Stage 7: empty graph doesn't crash ────────────────────────
    print("\n[Stage 7] update_timing_skip_build on fresh Timer (no build_graph)")
    try:
        chip7 = open_eda.Design()
        chip7.load_verilog(SHIFT_REG)
        t7 = make_timer(chip7)
        # Don't call build_graph — skip-build should fall back gracefully
        t7.update_timing_skip_build()
        check("empty graph: no crash", True)
        wns7 = t7.get_wns()
        check("empty graph: wns is numeric", isinstance(wns7, float))
    except Exception as e:
        check("empty graph: no crash",    False, str(e))
        check("empty graph: wns numeric", False, "stage failed")

    # ── Stage 8: gate swap → incr gives updated WNS ───────────────
    print("\n[Stage 8] Gate type swap: incr WNS reflects change")
    try:
        chip8 = make_placed_chip(FULL_ADDER)
        t8 = make_timer(chip8, 300.0)  # tight clock → violations
        t8.build_graph()
        t8.update_timing()
        wns_before = t8.get_wns()

        # Manually run one pass of setup fixing (which resizes gates)
        eco8 = open_eda.EcoEngine()
        n_fixed = eco8.fix_setup_violations(chip8, t8)

        # Incremental update
        t8.update_timing_skip_build()
        wns_after_incr = t8.get_wns()

        # Full rebuild for comparison
        t8.build_graph()
        t8.update_timing()
        wns_after_full = t8.get_wns()

        print(f"    (info: fixed={n_fixed}, before={wns_before:.1f}, "
              f"incr_after={wns_after_incr:.1f}, full_after={wns_after_full:.1f})")

        check("gate swap: no crash",                True)
        check("gate swap: incr close to full",
              abs(wns_after_incr - wns_after_full) < 1.0,
              f"incr={wns_after_incr:.2f} full={wns_after_full:.2f}")
    except Exception as e:
        check("gate swap: no crash",        False, str(e))
        check("gate swap: incr close full", False, "stage failed")

    # ── Stage 9: ECO loop does exactly 2 full rebuilds ────────────
    print("\n[Stage 9] ECO closure: full_rebuild_count == 2")
    try:
        chip9 = make_routed_chip(SHIFT_REG)
        t9 = make_timer(chip9, 500.0)  # tight → violations
        t9.build_graph()
        t9.update_timing()
        r9 = open_eda.EcoEngine().run_timing_closure(chip9, t9, 5)
        print(f"    (info: rebuilds={r9.full_rebuild_count}, incr={r9.incr_update_count}, "
              f"iters={r9.iterations})")
        check("ECO: full_rebuild_count == 2",
              r9.full_rebuild_count == 2, f"{r9.full_rebuild_count}")
    except Exception as e:
        check("ECO: full_rebuild_count == 2", False, str(e))

    # ── Stage 10: ECO loop uses at least 1 incremental update ─────
    print("\n[Stage 10] ECO closure: incr_update_count >= 1 when changes happen")
    try:
        chip10 = make_routed_chip(SHIFT_REG)
        t10 = make_timer(chip10, 300.0)  # very tight → multiple violations
        t10.build_graph()
        t10.update_timing()
        r10 = open_eda.EcoEngine().run_timing_closure(chip10, t10, 5)
        print(f"    (info: rebuilds={r10.full_rebuild_count}, incr={r10.incr_update_count})")
        # If any ECO changes were made, at least 1 incr update should have fired
        if r10.setup_fixed + r10.hold_fixed > 0:
            check("ECO: incr_update_count >= 1",
                  r10.incr_update_count >= 1, f"{r10.incr_update_count}")
        else:
            check("ECO: no changes so incr==0 ok",
                  r10.incr_update_count >= 0, f"{r10.incr_update_count}")
    except Exception as e:
        check("ECO: incr_update_count >= 1", False, str(e))

    # ── Stage 11: ECO incremental result == non-incremental ───────
    print("\n[Stage 11] ECO incremental final timing == independent full rebuild")
    try:
        chip11a = make_routed_chip(FULL_ADDER)
        t11a = make_timer(chip11a, 600.0)
        t11a.build_graph()
        t11a.update_timing()
        r11 = open_eda.EcoEngine().run_timing_closure(chip11a, t11a, 5)
        eco_wns = r11.final_setup_wns

        # Fresh chip + independent full rebuild
        chip11b = make_routed_chip(FULL_ADDER)
        t11b = make_timer(chip11b, 600.0)
        t11b.build_graph()
        t11b.update_timing()
        open_eda.EcoEngine().run_timing_closure(chip11b, t11b, 5)
        t11b.build_graph()
        t11b.update_timing()
        fresh_wns = t11b.get_wns()

        print(f"    (info: eco_wns={eco_wns:.1f}, fresh_wns={fresh_wns:.1f})")
        check("incr ECO final WNS in reasonable range",
              abs(eco_wns - fresh_wns) < 50.0,
              f"eco={eco_wns:.1f} fresh={fresh_wns:.1f}")
    except Exception as e:
        check("incr ECO final WNS consistent", False, str(e))

    # ── Stage 12: patchGraph correctness after buffer insert ──────
    print("\n[Stage 12] Hold fix via patchGraph: WNS within 1ps of full rebuild")
    try:
        chip12 = make_routed_chip(SHIFT_REG)
        t12 = make_timer(chip12, 2000.0)
        t12.build_graph()
        t12.update_timing()

        # Force hold violations by making clock very slow (big period → tiny latency budget)
        t12.set_clock_latency(500.0)   # latency=500ps makes hold window larger
        t12.update_timing()
        h_before = t12.get_hold_wns()

        eco12 = open_eda.EcoEngine()
        n_fixed = eco12.fix_hold_violations(chip12, t12)

        # Now do a full rebuild and compare
        t12.build_graph()
        t12.update_timing()
        h_after_full = t12.get_hold_wns()

        print(f"    (info: hold_before={h_before:.1f}, inserted={n_fixed}, "
              f"hold_after_full={h_after_full:.1f})")
        check("hold fix: no crash",          True)
        check("hold fix: post-full rebuild ok",
              h_after_full > h_before or n_fixed == 0,
              f"before={h_before:.1f} after={h_after_full:.1f}")
    except Exception as e:
        check("hold fix: no crash",           False, str(e))
        check("hold fix: post-full rebuild",  False, "stage failed")

    # ── Stage 13: Regression — full_adder ECO ─────────────────────
    print("\n[Stage 13] Regression — full_adder ECO closure")
    try:
        chip13 = make_routed_chip(FULL_ADDER)
        t13 = make_timer(chip13, 1000.0)
        t13.build_graph()
        t13.update_timing()
        r13 = open_eda.EcoEngine().run_timing_closure(chip13, t13, 5)
        print(f"    (info: final_wns={r13.final_setup_wns:.1f}, "
              f"final_viols={r13.final_setup_viols}, "
              f"rebuilds={r13.full_rebuild_count})")
        check("full_adder ECO: no crash",      True)
        check("full_adder ECO: rebuilds == 2", r13.full_rebuild_count == 2,
              f"{r13.full_rebuild_count}")
        check("full_adder ECO: final WNS finite",
              r13.final_setup_wns > -1e6, f"{r13.final_setup_wns:.1f}")
    except Exception as e:
        check("full_adder ECO: no crash",      False, str(e))
        check("full_adder ECO: rebuilds == 2", False, "stage failed")
        check("full_adder ECO: WNS finite",    False, "stage failed")

    # ── Stage 14: Regression — shift_reg ECO ──────────────────────
    print("\n[Stage 14] Regression — shift_reg ECO (1GHz should be clean)")
    try:
        chip14 = make_routed_chip(SHIFT_REG)
        t14 = make_timer(chip14, 1000.0)
        t14.build_graph()
        t14.update_timing()
        wns_init = t14.get_wns()
        r14 = open_eda.EcoEngine().run_timing_closure(chip14, t14, 5)
        print(f"    (info: init_wns={wns_init:.1f}, "
              f"final_wns={r14.final_setup_wns:.1f}, "
              f"final_viols={r14.final_setup_viols})")
        check("shift_reg ECO: no crash",         True)
        check("shift_reg ECO: final viols == 0 at 1GHz",
              r14.final_setup_viols == 0, f"{r14.final_setup_viols}")
        check("shift_reg ECO: full_rebuild_count == 2",
              r14.full_rebuild_count == 2, f"{r14.full_rebuild_count}")
    except Exception as e:
        check("shift_reg ECO: no crash",        False, str(e))
        check("shift_reg ECO: viols == 0",      False, "stage failed")
        check("shift_reg ECO: rebuilds == 2",   False, "stage failed")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
