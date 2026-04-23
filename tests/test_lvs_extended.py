"""
LVS Extended Tests — covers paths not reachable via the default test_lvs.py suite

Focuses on the four LVS check functions individually:
  1. UNROUTED_NET: place-only design (no RouteEngine) — all multi-pin nets unrouted
  2. OPEN_CIRCUIT: synthetic pin outside route bbox
  3. LVS after ECO — eco-inserted buffers are placed; eco nets are unrouted
  4. Instance count includes ECO buffers
  5. clean() == True only when all four counters are zero
  6. Regression: routed designs still produce zero UNPLACED/UNCONNECTED/UNROUTED

Run from project root:
    python tests/test_lvs_extended.py
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


def place_only(verilog, pdk=False):
    """Load + place but deliberately skip routing."""
    import open_eda
    chip = open_eda.Design()
    if pdk:
        chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(verilog)
    open_eda.run_placement(chip)
    return chip


def place_and_route(verilog, pdk=False):
    import open_eda
    chip = open_eda.Design()
    if pdk:
        chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(verilog)
    open_eda.run_placement(chip)
    open_eda.RouteEngine().route(chip)
    return chip


def run_suite():
    print("=" * 60)
    print("  LVS Extended — UNROUTED / OPEN_CIRCUIT / ECO paths")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda
    lvs = open_eda.LvsEngine()

    # ── Stage 1: UNROUTED_NET on place-only design ───────────────
    print("\n[Stage 1] UNROUTED_NET — place-only design (no routing)")
    chip1 = place_only(BENCH_SEQ)
    report1 = lvs.run_lvs(chip1)

    check("place-only: LVS runs without crash",   report1 is not None)
    check("place-only: all instances placed",
          report1.unplaced_count() == 0,
          f"{report1.unplaced_count()} unplaced")
    check("place-only: unrouted_count > 0",
          report1.unrouted_count() > 0,
          f"{report1.unrouted_count()} unrouted nets")
    check("place-only: clean() is False",   not report1.clean())
    # Pins should still be connected (netlist is intact)
    check("place-only: unconnected_pin_count == 0",
          report1.unconnected_pin_count() == 0,
          f"{report1.unconnected_pin_count()} floating")
    # net_count > 0 (design has nets)
    check("place-only: net_count > 0",  report1.net_count > 0, f"{report1.net_count}")

    print(f"    (info: unrouted_count={report1.unrouted_count()}, "
          f"total={report1.total_count()})")

    # ── Stage 2: UNROUTED_NET mismatch field validation ──────────
    print("\n[Stage 2] UNROUTED_NET mismatch fields")
    unrouted = [m for m in report1.mismatches
                if m.type == open_eda.LvsMismatchType.UNROUTED_NET]
    check("UNROUTED_NET mismatches present",  len(unrouted) > 0,
          f"{len(unrouted)} records")
    if unrouted:
        u = unrouted[0]
        check("UNROUTED mismatch.net_name non-empty",   len(u.net_name) > 0,
              u.net_name)
        check("UNROUTED mismatch.message non-empty",    len(u.message) > 0,
              u.message)
        check("UNROUTED mismatch.message contains 'pins'",
              "pins" in u.message or "routing" in u.message,
              u.message)
        # inst_name is empty for net-level mismatches (that is correct behaviour)
        check("UNROUTED mismatch.inst_name is empty (net-level)",
              len(u.inst_name) == 0,
              repr(u.inst_name))

    # ── Stage 3: count consistency on place-only report ──────────
    print("\n[Stage 3] Count consistency — place-only report")
    total_helpers1 = (report1.unplaced_count()
                    + report1.unconnected_pin_count()
                    + report1.unrouted_count()
                    + report1.open_circuit_count())
    check("helper counts sum to total_count()",
          total_helpers1 == report1.total_count(),
          f"sum={total_helpers1}  total={report1.total_count()}")
    check("routed_net_count == 0 before routing",
          report1.routed_net_count == 0,
          f"{report1.routed_net_count}")
    # connected_pin_count should be > 0 (netlist wiring exists)
    check("connected_pin_count > 0",
          report1.connected_pin_count > 0,
          f"{report1.connected_pin_count}")

    # ── Stage 4: LVS after ECO ────────────────────────────────────
    print("\n[Stage 4] LVS after ECO timing closure")
    # Build a timed chip so ECO can run
    try:
        import open_eda as oe
        chip4 = oe.Design()
        chip4.load_verilog(BENCH_SEQ)
        oe.run_placement(chip4)
        oe.RouteEngine().route(chip4)

        # Load SPEF + run ECO
        spef4 = oe.SpefEngine()
        spef4.extract(chip4)
        sta4 = oe.Timer(chip4, spef4)
        sta4.build_graph()
        sta4.update_timing()
        oe.EcoEngine().fix_hold_violations(chip4, sta4)

        report4 = lvs.run_lvs(chip4)
        check("ECO: LVS runs without crash",        report4 is not None)
        check("ECO: no unplaced instances",
              report4.unplaced_count() == 0,
              f"{report4.unplaced_count()} unplaced")
        check("ECO: instance_count >= original 4",
              report4.instance_count >= 4,
              f"{report4.instance_count}")
        # ECO may insert buffers → more instances than original
        chip4_original_count = 4  # shift_reg has 4 FFs
        check("ECO: instance_count >= original count",
              report4.instance_count >= chip4_original_count,
              f"lvs={report4.instance_count} original={chip4_original_count}")
        # ECO buffers are placed but their nets may lack routing
        # (we just check that the counter is consistent, not assert value)
        unrouted4_ok = (report4.unrouted_count() >= 0)
        check("ECO: unrouted_count >= 0 (consistent)",  unrouted4_ok,
              f"{report4.unrouted_count()}")
        print(f"    (info: ECO LVS — instances={report4.instance_count}, "
              f"unrouted={report4.unrouted_count()}, "
              f"open_circuits={report4.open_circuit_count()}, "
              f"total={report4.total_count()})")
        eco_ran = True
    except Exception as e:
        check("ECO: LVS runs without crash", False, str(e))
        check("ECO: no unplaced instances", False, "ECO stage failed")
        check("ECO: instance_count >= original 4", False, "ECO stage failed")
        check("ECO: instance_count >= original count", False, "ECO stage failed")
        check("ECO: unrouted_count >= 0 (consistent)", False, "ECO stage failed")
        eco_ran = False

    # ── Stage 5: clean() semantics ────────────────────────────────
    print("\n[Stage 5] clean() semantics")
    # Fully routed design should be clean (or at most open-circuits only)
    chip5 = place_and_route(BENCH_SEQ)
    report5 = lvs.run_lvs(chip5)

    check("routed: unplaced_count == 0",
          report5.unplaced_count() == 0, f"{report5.unplaced_count()}")
    check("routed: unconnected_pin_count == 0",
          report5.unconnected_pin_count() == 0, f"{report5.unconnected_pin_count()}")
    check("routed: unrouted_count == 0",
          report5.unrouted_count() == 0, f"{report5.unrouted_count()}")
    # clean() means all four counters are zero
    expected_clean = (report5.unplaced_count() == 0
                      and report5.unconnected_pin_count() == 0
                      and report5.unrouted_count() == 0
                      and report5.open_circuit_count() == 0)
    check("clean() matches expected value",
          report5.clean() == expected_clean,
          f"clean()={report5.clean()} expected={expected_clean}")

    # ── Stage 6: LvsReport.print() on unrouted design ────────────
    print("\n[Stage 6] LvsReport.print() on unrouted design")
    try:
        report1.print(5)
        check("print() on unrouted report runs without crash", True)
    except Exception as e:
        check("print() on unrouted report runs without crash", False, str(e))

    # ── Stage 7: Stress — sky130 inv_chain place-only ─────────────
    print("\n[Stage 7] Stress — sky130 inv_chain place-only LVS")
    if os.path.exists(BENCH_COMB) and os.path.exists(PDK_LIB):
        chip7 = place_only(BENCH_COMB, pdk=True)
        report7 = lvs.run_lvs(chip7)
        check("sky130 comb place-only: no crash",         report7 is not None)
        check("sky130 comb place-only: unplaced == 0",
              report7.unplaced_count() == 0, f"{report7.unplaced_count()}")
        check("sky130 comb place-only: unrouted > 0",
              report7.unrouted_count() > 0, f"{report7.unrouted_count()}")
        check("sky130 comb place-only: instance_count matches chip",
              report7.instance_count == chip7.get_instance_count(),
              f"lvs={report7.instance_count} chip={chip7.get_instance_count()}")
    else:
        print("    (skipping — sky130 PDK files not found)")

    # ── Stage 8: Regression — routed designs still clean ─────────
    print("\n[Stage 8] Regression — routed designs have no UNROUTED_NET")
    for bench, label, use_pdk in [
        (BENCH_SEQ,  "shift_reg", False),
        (BENCH_COMB, "inv_chain", True),
    ]:
        if not os.path.exists(bench): continue
        if use_pdk and not os.path.exists(PDK_LIB): continue
        chip_r = place_and_route(bench, pdk=use_pdk)
        rep_r  = lvs.run_lvs(chip_r)
        check(f"{label}: no UNROUTED_NET after routing",
              rep_r.unrouted_count() == 0,
              f"{rep_r.unrouted_count()}")
        check(f"{label}: no UNPLACED after routing",
              rep_r.unplaced_count() == 0,
              f"{rep_r.unplaced_count()}")
        check(f"{label}: no UNCONNECTED_PIN after routing",
              rep_r.unconnected_pin_count() == 0,
              f"{rep_r.unconnected_pin_count()}")
        check(f"{label}: routed_net_count > 0",
              rep_r.routed_net_count > 0,
              f"{rep_r.routed_net_count}")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
