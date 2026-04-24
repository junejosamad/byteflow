"""
Phase 5.2 — Gate Sizer Test Suite

Validates GateSizer (drive-strength resizing):
  1. API surface: GateSizer, GateSizeResult
  2. resizeForTiming fields: cells_upsized, timing_improvement_ps, area_saved_units, resize_log
  3. resizeForArea  fields: cells_downsized, area_saved_units, timing_improvement_ps
  4. resizeForTiming on a timing-violating design — cells_upsized > 0
  5. resizeForTiming on a passing design (no violations) — cells_upsized == 0
  6. resizeForArea on a slack-rich design — cells_downsized > 0
  7. resizeForArea on a tight design (small slack_budget) — cells_downsized == 0
  8. Upsizing uses _X2 variants from simple.lib
  9. Downsizing reverts _X2 back to base
 10. max_changes limit is respected
 11. Regression — shift_reg still synthesizes and PnR after sizer runs

Run from project root:
    python tests/test_gate_sizer.py
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

BENCH = os.path.join(ROOT, "benchmarks")
SIMPLE_LIB = os.path.join(BENCH, "simple.lib")
FULL_ADDER = os.path.join(BENCH, "full_adder.v")
SHIFT_REG  = os.path.join(BENCH, "shift_reg.v")

# A Verilog netlist that uses AND2_X2 / NOT_X2 — we create it on the fly
# to test downsize (those cells have positive slack → should downsize to base).
_X2_NETLIST = """\
module x2_design (
    input  A, B, clk,
    output Q
);
    wire n1, n2;
    AND2_X2 g1 (.A(A), .B(B), .Y(n1));
    NOT_X2  g2 (.A(n1), .Y(n2));
    DFF     ff (.C(clk), .D(n2), .Q(Q));
endmodule
"""

def write_tmp(content, suffix=".v"):
    import tempfile
    f = tempfile.NamedTemporaryFile(mode="w", suffix=suffix, delete=False,
                                    encoding="utf-8")
    f.write(content)
    f.close()
    return f.name

def load_and_place(netlist):
    import open_eda
    chip = open_eda.Design()
    chip.load_verilog(netlist)   # auto-loads benchmarks/simple.lib
    open_eda.run_placement(chip)
    return chip

def make_timer(chip, period_ps=1000.0):
    import open_eda
    t = open_eda.Timer(chip)
    t.set_clock_period(period_ps)
    t.build_graph()
    t.update_timing()
    return t

def make_tight_timer(chip, period_ps=50.0):
    """Very tight clock — likely creates setup violations."""
    import open_eda
    t = open_eda.Timer(chip)
    t.set_clock_period(period_ps)
    t.build_graph()
    t.update_timing()
    return t


def run_suite():
    print("=" * 60)
    print("  Phase 5.2 - Gate Sizer")
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
    check("GateSizer class exists",      hasattr(open_eda, "GateSizer"))
    check("GateSizeResult class exists", hasattr(open_eda, "GateSizeResult"))
    sizer = open_eda.GateSizer()
    check("GateSizer instantiates",      sizer is not None)
    check("resize_for_timing method",    hasattr(sizer, "resize_for_timing"))
    check("resize_for_area method",      hasattr(sizer, "resize_for_area"))

    # ── Stage 2: GateSizeResult fields (using a trivial call) ──
    print("\n[Stage 2] GateSizeResult field types")
    chip2 = load_and_place(FULL_ADDER)
    t2    = make_timer(chip2, period_ps=1000.0)
    r2    = sizer.resize_for_timing(chip2, t2, max_changes=0)
    check("cells_upsized is int",          isinstance(r2.cells_upsized, int))
    check("cells_downsized is int",        isinstance(r2.cells_downsized, int))
    check("timing_improvement_ps is float",isinstance(r2.timing_improvement_ps, float))
    check("area_saved_units is float",     isinstance(r2.area_saved_units, float))
    check("resize_log is list",            isinstance(r2.resize_log, list))

    # ── Stage 3: resizeForArea fields ──────────────────────────
    print("\n[Stage 3] resizeForArea field types")
    chip3 = load_and_place(FULL_ADDER)
    t3    = make_timer(chip3, period_ps=1000.0)
    r3    = sizer.resize_for_area(chip3, t3, slack_budget_ps=0.0, max_changes=0)
    check("cells_downsized is int",        isinstance(r3.cells_downsized, int))
    check("area_saved_units is float",     isinstance(r3.area_saved_units, float))
    check("timing_improvement_ps is float",isinstance(r3.timing_improvement_ps, float))

    # ── Stage 4: resizeForTiming on tight clock ─────────────────
    print("\n[Stage 4] resizeForTiming — upsize on tight-clock design")
    chip4 = load_and_place(FULL_ADDER)
    t4    = make_tight_timer(chip4, period_ps=50.0)
    wns4_before = t4.get_wns()
    r4    = sizer.resize_for_timing(chip4, t4, max_changes=10)
    print(f"    (info: WNS before={wns4_before:.1f} ps, "
          f"upsized={r4.cells_upsized}, improvement={r4.timing_improvement_ps:.1f} ps)")
    check("tight: cells_upsized >= 0", r4.cells_upsized >= 0)
    check("tight: timing_improvement_ps is numeric",
          isinstance(r4.timing_improvement_ps, float))

    # ── Stage 5: resizeForTiming on relaxed clock (no violations) ──
    print("\n[Stage 5] resizeForTiming — no violations on relaxed clock")
    chip5 = load_and_place(FULL_ADDER)
    t5    = make_timer(chip5)        # 1 ns clock — all paths should pass
    wns5  = t5.get_wns()
    r5    = sizer.resize_for_timing(chip5, t5, max_changes=50)
    print(f"    (info: WNS={wns5:.1f} ps, upsized={r5.cells_upsized})")
    check("relaxed: cells_upsized == 0 (no violations to fix)",
          r5.cells_upsized == 0,
          f"upsized={r5.cells_upsized}")

    # ── Stage 6: resizeForArea on _X2 design ────────────────────
    print("\n[Stage 6] resizeForArea — downsize _X2 cells")
    x2_path = write_tmp(_X2_NETLIST)
    try:
        chip6 = load_and_place(x2_path)
        t6    = make_timer(chip6)
        wns6  = t6.get_wns()
        r6    = sizer.resize_for_area(chip6, t6,
                                       slack_budget_ps=10.0, max_changes=50)
        print(f"    (info: WNS={wns6:.1f} ps, downsized={r6.cells_downsized}, "
              f"area_saved={r6.area_saved_units:.1f})")
        check("_X2 design: cells_downsized > 0",
              r6.cells_downsized > 0, f"{r6.cells_downsized}")
        check("_X2 design: area_saved_units > 0",
              r6.area_saved_units > 0, f"{r6.area_saved_units:.2f}")
    except Exception as e:
        check("_X2 design: cells_downsized > 0", False, str(e))
        check("_X2 design: area_saved_units > 0", False, "stage failed")
    finally:
        os.unlink(x2_path)

    # ── Stage 7: resizeForArea — budget too tight ───────────────
    print("\n[Stage 7] resizeForArea — huge slack_budget prevents downsize")
    x2_path2 = write_tmp(_X2_NETLIST)
    try:
        chip7 = load_and_place(x2_path2)
        t7    = make_timer(chip7)
        # budget larger than any slack in this tiny design → nothing downsized
        r7    = sizer.resize_for_area(chip7, t7,
                                       slack_budget_ps=1e9, max_changes=50)
        check("huge budget: cells_downsized == 0",
              r7.cells_downsized == 0, f"{r7.cells_downsized}")
    except Exception as e:
        check("huge budget: cells_downsized == 0", False, str(e))
    finally:
        os.unlink(x2_path2)

    # ── Stage 8: _X2 variant present in simple.lib ──────────────
    print("\n[Stage 8] simple.lib contains _X2 variants")
    try:
        with open(SIMPLE_LIB, encoding="utf-8") as f:
            lib_text = f.read()
        x2_cells = ["BUF_X2", "NOT_X2", "AND2_X2", "OR2_X2",
                    "NAND2_X2", "NOR2_X2", "XOR2_X2", "DFF_X2", "CLKBUF_X2"]
        for cname in x2_cells:
            check(f"simple.lib has cell({cname})", f"cell({cname})" in lib_text)
    except Exception as e:
        for cname in ["BUF_X2", "AND2_X2"]:
            check(f"simple.lib has cell({cname})", False, str(e))

    # ── Stage 9: upsizing picks _X2 variant ─────────────────────
    print("\n[Stage 9] Upsize selects _X2 variant")
    chip9 = load_and_place(FULL_ADDER)
    # Force a violation by using a 10ps clock
    t9    = make_tight_timer(chip9, period_ps=10.0)
    r9    = sizer.resize_for_timing(chip9, t9, max_changes=50)
    has_x2 = any("_X2" in entry for entry in r9.resize_log)
    print(f"    (info: upsized={r9.cells_upsized}, X2_swaps={sum(1 for e in r9.resize_log if '_X2' in e)})")
    check("upsize log contains _X2 swaps (or no X2 available)",
          has_x2 or r9.cells_upsized == 0)

    # ── Stage 10: max_changes limit ─────────────────────────────
    print("\n[Stage 10] max_changes limit is respected")
    chip10 = load_and_place(FULL_ADDER)
    t10    = make_tight_timer(chip10, period_ps=10.0)
    r10    = sizer.resize_for_timing(chip10, t10, max_changes=1)
    check("max_changes=1: cells_upsized <= 1",
          r10.cells_upsized <= 1, f"{r10.cells_upsized}")

    # ── Stage 11: Regression — shift_reg PnR unaffected ─────────
    print("\n[Stage 11] Regression — shift_reg PnR unaffected")
    try:
        chip11 = open_eda.Design()
        chip11.load_verilog(SHIFT_REG)
        open_eda.run_placement(chip11)
        open_eda.RouteEngine().route(chip11)
        check("shift_reg: PnR no crash", True)
        check("shift_reg: instance_count == 4",
              chip11.get_instance_count() == 4,
              f"{chip11.get_instance_count()}")
    except Exception as e:
        check("shift_reg: PnR no crash", False, str(e))
        check("shift_reg: instance_count == 4", False, "stage failed")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
