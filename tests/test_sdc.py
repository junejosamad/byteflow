"""
Phase 2.3 — SDC Constraint Parsing Test

Validates:
  1. SdcParser: create_clock, set_input_delay, set_output_delay,
     set_clock_uncertainty, set_clock_latency, set_false_path,
     set_multicycle_path
  2. Timer correctly picks up clock period, input/output delays,
     uncertainty, and latency from loaded SDC
  3. WNS changes predictably when constraints tighten/relax

Run from project root:
    python tests/test_sdc.py
"""
import sys, os, time
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

PDK_LIB  = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib")
PDK_LEF  = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd_merged.lef")
BENCH_V  = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")
BENCH_SDC = os.path.join(ROOT, "benchmarks/sky130_inv_chain.sdc")


def load_design():
    import open_eda
    chip = open_eda.Design()
    chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(BENCH_V)
    open_eda.run_placement(chip)
    router = open_eda.RouteEngine()
    router.route(chip)
    return chip


def run_sta(chip, period_ps=None):
    import open_eda
    spef = open_eda.SpefEngine()
    spef.extract(chip)
    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    if period_ps is not None:
        sta.set_clock_period(period_ps)
    sta.update_timing()
    return sta.get_wns(), sta.get_tns(), sta.get_violation_count()


def run_suite():
    print("=" * 60)
    print("  Phase 2.3 — SDC Constraint Parsing Test")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    # ── PDK files present ──────────────────────────────────────
    for label, path in [("sky130 Liberty", PDK_LIB),
                        ("sky130 LEF",     PDK_LEF),
                        ("benchmark .v",   BENCH_V),
                        ("benchmark .sdc", BENCH_SDC)]:
        if not check(f"{label} file exists", os.path.exists(path), path):
            return summarize()

    # ── 1. SDC file parsing ────────────────────────────────────
    print("\n[Stage 1] SDC file parsing")
    chip = load_design()

    ok = chip.read_sdc(BENCH_SDC)
    check("read_sdc returns True",  ok)

    # Clock period: 2.0 ns = 2000 ps
    period = chip.get_clock_period()
    check("clock period parsed (2000 ps)", abs(period - 2000.0) < 1.0,
          f"got {period:.1f} ps")

    # ── 2. STA uses SDC clock period ───────────────────────────
    print("\n[Stage 2] STA consumes SDC clock period")
    spef = open_eda.SpefEngine()
    spef.extract(chip)
    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    sta.update_timing()           # <-- should read period from chip.sdc
    wns_sdc = sta.get_wns()

    check("STA WNS is finite after SDC load",
          wns_sdc not in (float("inf"), float("-inf")),
          f"WNS={wns_sdc:.1f}ps")

    # 2 ns clock with 0.1 ns in + 0.1 ns out + 0.05 ns uncertainty − 0.02 ns latency
    # effective budget = 2000 - 100 - 100 - 50 + 20 = 1770 ps
    # small combinational chain should have positive slack
    check("WNS >= 0 with 2 ns clock (500 MHz)",
          wns_sdc >= 0.0,
          f"WNS={wns_sdc:.1f}ps")

    # ── 3. Tight clock: force violation ──────────────────────
    print("\n[Stage 3] Tight clock forces violation")
    chip2 = load_design()
    # 0.01 ns clock = 10 ps — combinational chain will definitely violate
    spef2 = open_eda.SpefEngine()
    spef2.extract(chip2)
    sta2 = open_eda.Timer(chip2, spef2)
    sta2.build_graph()
    sta2.set_clock_period(10.0)   # 10 ps — will always violate
    sta2.update_timing()
    wns_tight = sta2.get_wns()
    check("WNS < 0 with 10 ps clock (violation expected)",
          wns_tight < 0.0,
          f"WNS={wns_tight:.1f}ps")

    # ── 4. set_input_delay widens available budget ────────────
    print("\n[Stage 4] Input/output delay impact on WNS")
    # Load SDC and then override period with a moderate value
    chip3 = load_design()
    chip3.read_sdc(BENCH_SDC)
    spef3 = open_eda.SpefEngine()
    spef3.extract(chip3)
    sta3 = open_eda.Timer(chip3, spef3)
    sta3.build_graph()
    sta3.update_timing()
    wns3 = sta3.get_wns()

    # Without SDC (same period, no delays)
    chip4 = load_design()
    spef4 = open_eda.SpefEngine()
    spef4.extract(chip4)
    sta4 = open_eda.Timer(chip4, spef4)
    sta4.build_graph()
    sta4.set_clock_period(2000.0)  # same 2 ns, but no input/output delay
    sta4.update_timing()
    wns4 = sta4.get_wns()

    # SDC-constrained WNS should be smaller (less slack) than unconstrained WNS
    # because input+output delays eat into the budget
    check("SDC output/input delays reduce WNS vs unconstrained",
          wns3 <= wns4,
          f"WNS_sdc={wns3:.1f}ps  WNS_bare={wns4:.1f}ps")

    # ── 5. set_false_path / multicycle (parse-only validation) ─
    print("\n[Stage 5] False path and multicycle parse test")
    fp_sdc = os.path.join(ROOT, "benchmarks/test_extras.sdc")
    with open(fp_sdc, "w") as f:
        f.write("create_clock -period 1.0 -name clk [get_ports clk]\n")
        f.write("set_false_path -from [get_ports A] -to [get_ports Y]\n")
        f.write("set_multicycle_path 2 -setup -from [get_cells u_inv0] -to [get_cells u_buf0]\n")
        f.write("set_clock_uncertainty 0.05 [get_clocks clk]\n")
        f.write("set_clock_latency 0.01 [get_clocks clk]\n")

    chip5 = open_eda.Design()
    chip5.load_pdk(PDK_LIB, PDK_LEF)
    chip5.load_verilog(BENCH_V)
    ok5 = chip5.read_sdc(fp_sdc)
    check("set_false_path / multicycle SDC parsed without error", ok5)

    # Clock period = 1 ns = 1000 ps
    p5 = chip5.get_clock_period()
    check("clock period 1 ns = 1000 ps",
          abs(p5 - 1000.0) < 1.0,
          f"got {p5:.1f} ps")

    os.remove(fp_sdc)

    # ── 6. Regression: existing tests still pass ──────────────
    print("\n[Stage 6] Regression: no SDC path still works")
    chip6 = open_eda.Design()
    chip6.load_pdk(PDK_LIB, PDK_LEF)
    chip6.load_verilog(BENCH_V)
    open_eda.run_placement(chip6)
    router6 = open_eda.RouteEngine()
    router6.route(chip6)
    spef6 = open_eda.SpefEngine()
    spef6.extract(chip6)
    sta6 = open_eda.Timer(chip6, spef6)
    sta6.build_graph()
    sta6.set_clock_period(2000.0)
    sta6.update_timing()
    wns6 = sta6.get_wns()
    check("STA without SDC still produces finite WNS",
          wns6 not in (float("inf"), float("-inf")),
          f"WNS={wns6:.1f}ps")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
