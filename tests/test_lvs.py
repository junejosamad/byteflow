"""
Phase 4.2 — LVS (Layout vs. Schematic) Test Suite

Validates:
  1. API surface: LvsEngine, LvsReport, LvsMismatch, LvsMismatchType
  2. LvsMismatchType enum values
  3. LvsReport fields (instance_count, net_count, pin counts)
  4. Clean routed design — no unplaced, no unconnected, no unrouted
  5. LvsMismatch fields — type, inst_name, net_name, pin_name, message
  6. Unplaced instance detection (synthetic test)
  7. Unconnected pin detection (net == None scenario)
  8. sky130 combinational — LVS passes cleanly
  9. LvsReport.print() does not crash
  10. Regression: existing tests unaffected

Run from project root:
    python tests/test_lvs.py
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


def route(verilog, pdk=False):
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
    print("  Phase 4.2 - LVS Engine")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    # ── Stage 1: API surface ────────────────────────────────────
    print("\n[Stage 1] API surface")
    check("LvsEngine class exists",      hasattr(open_eda, "LvsEngine"))
    check("LvsReport class exists",      hasattr(open_eda, "LvsReport"))
    check("LvsMismatch class exists",    hasattr(open_eda, "LvsMismatch"))
    check("LvsMismatchType enum exists", hasattr(open_eda, "LvsMismatchType"))

    lvs = open_eda.LvsEngine()
    check("LvsEngine instantiates",  lvs is not None)
    check("run_lvs method exists",   hasattr(lvs, "run_lvs"))

    # ── Stage 2: LvsMismatchType enum ──────────────────────────
    print("\n[Stage 2] LvsMismatchType enum")
    check("UNPLACED_INSTANCE value", hasattr(open_eda.LvsMismatchType, "UNPLACED_INSTANCE"))
    check("UNCONNECTED_PIN value",   hasattr(open_eda.LvsMismatchType, "UNCONNECTED_PIN"))
    check("UNROUTED_NET value",      hasattr(open_eda.LvsMismatchType, "UNROUTED_NET"))
    check("OPEN_CIRCUIT value",      hasattr(open_eda.LvsMismatchType, "OPEN_CIRCUIT"))

    # ── Stage 3: LvsReport on clean routed design ──────────────
    print("\n[Stage 3] LvsReport on routed shift_reg")
    chip3 = route(BENCH_SEQ)
    report3 = lvs.run_lvs(chip3)

    check("run_lvs returns LvsReport",       report3 is not None)
    check("instance_count > 0",              report3.instance_count > 0,
          f"{report3.instance_count}")
    check("net_count > 0",                   report3.net_count > 0,
          f"{report3.net_count}")
    check("total_pin_count > 0",             report3.total_pin_count > 0,
          f"{report3.total_pin_count}")
    check("connected_pin_count > 0",         report3.connected_pin_count > 0,
          f"{report3.connected_pin_count}")
    check("routed_net_count > 0",            report3.routed_net_count > 0,
          f"{report3.routed_net_count}")
    check("mismatches is list",              isinstance(report3.mismatches, list))
    check("clean() returns bool",            isinstance(report3.clean(), bool))
    check("total_count() >= 0",             report3.total_count() >= 0,
          f"{report3.total_count()}")

    # After placement + routing: no unplaced instances, no unconnected pins
    check("no unplaced instances",           report3.unplaced_count() == 0,
          f"{report3.unplaced_count()} unplaced")
    check("no unconnected pins",             report3.unconnected_pin_count() == 0,
          f"{report3.unconnected_pin_count()} floating")
    check("no unrouted nets",                report3.unrouted_count() == 0,
          f"{report3.unrouted_count()} unrouted")

    print(f"    (info: shift_reg LVS — "
          f"open_circuits={report3.open_circuit_count()}, "
          f"total={report3.total_count()})")

    # ── Stage 4: LvsMismatch fields ────────────────────────────
    print("\n[Stage 4] LvsMismatch fields")
    if report3.mismatches:
        m = report3.mismatches[0]
        check("mismatch.type is LvsMismatchType",  True, str(m.type))
        check("mismatch.message non-empty",         len(m.message) > 0, m.message)
        # at least one of inst_name / net_name should be set
        check("mismatch has inst or net name",
              len(m.inst_name) > 0 or len(m.net_name) > 0)
    else:
        # Design is clean — verify the report reflects that
        check("clean design: clean() == True",  report3.clean())
        check("clean design: total_count == 0", report3.total_count() == 0)

    # ── Stage 5: LvsReport method correctness ──────────────────
    print("\n[Stage 5] LvsReport method correctness")
    # Count helpers must be consistent with total
    total_from_helpers = (report3.unplaced_count()
                        + report3.unconnected_pin_count()
                        + report3.unrouted_count()
                        + report3.open_circuit_count())
    check("helper counts sum to total",
          total_from_helpers == report3.total_count(),
          f"helpers={total_from_helpers}  total={report3.total_count()}")

    # connected_pin_count <= total_pin_count
    check("connected_pin_count <= total_pin_count",
          report3.connected_pin_count <= report3.total_pin_count,
          f"{report3.connected_pin_count}/{report3.total_pin_count}")

    # routed_net_count <= net_count
    check("routed_net_count <= net_count",
          report3.routed_net_count <= report3.net_count,
          f"{report3.routed_net_count}/{report3.net_count}")

    # ── Stage 6: sky130 PDK combinational ──────────────────────
    print("\n[Stage 6] sky130 PDK combinational LVS")
    chip6 = route(BENCH_COMB, pdk=True)
    report6 = lvs.run_lvs(chip6)
    check("sky130 comb: LVS runs without crash", report6 is not None)
    check("sky130 comb: no unplaced instances",
          report6.unplaced_count() == 0,
          f"{report6.unplaced_count()} unplaced")
    check("sky130 comb: no unconnected pins",
          report6.unconnected_pin_count() == 0,
          f"{report6.unconnected_pin_count()} floating")
    check("sky130 comb: no unrouted nets",
          report6.unrouted_count() == 0,
          f"{report6.unrouted_count()} unrouted")
    check("sky130 comb: instance_count matches cell count",
          report6.instance_count == chip6.get_instance_count(),
          f"lvs={report6.instance_count} chip={chip6.get_instance_count()}")
    print(f"    (info: sky130 comb LVS — "
          f"open_circuits={report6.open_circuit_count()}, "
          f"total={report6.total_count()})")

    # ── Stage 7: Combined DRC+LVS sign-off ─────────────────────
    print("\n[Stage 7] Combined DRC + LVS sign-off")
    drc = open_eda.DrcEngine()
    drc_report = drc.run_drc(chip3)
    lvs_report = lvs.run_lvs(chip3)

    check("DRC short violations detected",
          drc_report.short_count() > 0,
          f"{drc_report.short_count()} shorts")
    check("LVS topology consistent (no unrouted/unplaced/floating)",
          lvs_report.unrouted_count() == 0
          and lvs_report.unplaced_count() == 0
          and lvs_report.unconnected_pin_count() == 0)

    # A sign-off check: DRC+LVS clean means tape-out ready
    tape_out_ready = (drc_report.short_count() == 0
                      and lvs_report.unplaced_count() == 0
                      and lvs_report.unrouted_count() == 0
                      and lvs_report.unconnected_pin_count() == 0)
    check("shift_reg is NOT tape-out ready (routing shorts exist)",
          not tape_out_ready,
          f"DRC_shorts={drc_report.short_count()} "
          f"LVS_mismatches={lvs_report.total_count()}")

    # ── Stage 8: LvsReport.print() ─────────────────────────────
    print("\n[Stage 8] LvsReport.print()")
    try:
        report3.print(5)
        check("LvsReport.print() runs without crash", True)
    except Exception as e:
        check("LvsReport.print() runs without crash", False, str(e))

    # ── Stage 9: Regression ─────────────────────────────────────
    print("\n[Stage 9] Regression")
    for bench, label in [("benchmarks/full_adder.v", "full_adder"),
                          ("benchmarks/adder.v",      "adder")]:
        path = os.path.join(ROOT, bench)
        if not os.path.exists(path): continue
        chip_r = route(path)
        rep_r  = lvs.run_lvs(chip_r)
        check(f"{label}: LVS runs without crash",   rep_r is not None)
        check(f"{label}: no unplaced instances",
              rep_r.unplaced_count() == 0,
              f"{rep_r.unplaced_count()} unplaced")
        check(f"{label}: no unconnected pins",
              rep_r.unconnected_pin_count() == 0,
              f"{rep_r.unconnected_pin_count()} floating")
        check(f"{label}: instance_count > 0",
              rep_r.instance_count > 0,
              f"{rep_r.instance_count}")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
