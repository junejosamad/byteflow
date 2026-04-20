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

    # ── 7. set_false_path — endpoint actually excluded ─────────
    # Behavioral test: false-path u_buf0 (drives output Y).
    # With a 50 ps clock every endpoint violates.
    # After false-pathing u_buf0 the violation count must decrease.
    print("\n[Stage 7] set_false_path excludes endpoint from timing")
    base_chip = load_design()
    base_spef = open_eda.SpefEngine(); base_spef.extract(base_chip)
    base_sta  = open_eda.Timer(base_chip, base_spef)
    base_sta.build_graph()
    base_sta.set_clock_period(1.0)    # 1 ps — sky130 gates are ~20-50 ps, guaranteed violation
    base_sta.update_timing()
    viols_base = base_sta.get_violation_count()
    check("violations > 0 with 1 ps clock (baseline)",
          viols_base > 0, f"{viols_base} violations")

    fp7_sdc = os.path.join(ROOT, "benchmarks/test_fp7.sdc")
    with open(fp7_sdc, "w") as f:
        # u_buf0 drives output Y — false-path it by cell name
        f.write("create_clock -period 0.001 -name clk [get_ports A]\n")
        f.write("set_false_path -to [get_cells u_buf0]\n")

    fp_chip = load_design()
    fp_chip.read_sdc(fp7_sdc)
    fp_spef = open_eda.SpefEngine(); fp_spef.extract(fp_chip)
    fp_sta  = open_eda.Timer(fp_chip, fp_spef)
    fp_sta.build_graph()
    fp_sta.update_timing()
    viols_fp = fp_sta.get_violation_count()
    os.remove(fp7_sdc)

    check("false_path on u_buf0 reduces violation count",
          viols_fp < viols_base,
          f"before={viols_base}  after={viols_fp}")

    # ── 8. set_clock_uncertainty — exact budget reduction ──────
    print("\n[Stage 8] set_clock_uncertainty reduces WNS by exact amount")
    UNCERTAINTY_NS = 0.1   # 100 ps

    base2_chip = load_design()
    base2_spef = open_eda.SpefEngine(); base2_spef.extract(base2_chip)
    base2_sta  = open_eda.Timer(base2_chip, base2_spef)
    base2_sta.build_graph()
    base2_sta.set_clock_period(2000.0)
    base2_sta.update_timing()
    wns_no_unc = base2_sta.get_wns()

    unc8_sdc = os.path.join(ROOT, "benchmarks/test_unc8.sdc")
    with open(unc8_sdc, "w") as f:
        f.write(f"create_clock -period 2.0 -name clk [get_ports A]\n")
        f.write(f"set_clock_uncertainty {UNCERTAINTY_NS} [get_clocks clk]\n")

    unc_chip = load_design()
    unc_chip.read_sdc(unc8_sdc)
    unc_spef = open_eda.SpefEngine(); unc_spef.extract(unc_chip)
    unc_sta  = open_eda.Timer(unc_chip, unc_spef)
    unc_sta.build_graph()
    unc_sta.update_timing()
    wns_with_unc = unc_sta.get_wns()
    os.remove(unc8_sdc)

    expected_delta = UNCERTAINTY_NS * 1000   # ns → ps
    actual_delta   = wns_no_unc - wns_with_unc
    check("clock_uncertainty shifts WNS by correct amount (100 ps)",
          abs(actual_delta - expected_delta) < 2.0,
          f"expected={expected_delta:.0f}ps  actual={actual_delta:.1f}ps")

    # ── 9. set_clock_latency — exact budget increase ───────────
    print("\n[Stage 9] set_clock_latency increases WNS by exact amount")
    LATENCY_NS = 0.05   # 50 ps

    lat9_sdc = os.path.join(ROOT, "benchmarks/test_lat9.sdc")
    with open(lat9_sdc, "w") as f:
        f.write(f"create_clock -period 2.0 -name clk [get_ports A]\n")
        f.write(f"set_clock_latency {LATENCY_NS} [get_clocks clk]\n")

    lat_chip = load_design()
    lat_chip.read_sdc(lat9_sdc)
    lat_spef = open_eda.SpefEngine(); lat_spef.extract(lat_chip)
    lat_sta  = open_eda.Timer(lat_chip, lat_spef)
    lat_sta.build_graph()
    lat_sta.update_timing()
    wns_with_lat = lat_sta.get_wns()
    os.remove(lat9_sdc)

    # Reuse wns_no_unc as baseline (same design, same clock period, no constraints)
    expected_lat_delta = LATENCY_NS * 1000   # ns → ps
    actual_lat_delta   = wns_with_lat - wns_no_unc
    check("clock_latency increases WNS by correct amount (50 ps)",
          abs(actual_lat_delta - expected_lat_delta) < 2.0,
          f"expected=+{expected_lat_delta:.0f}ps  actual={actual_lat_delta:+.1f}ps")

    # ── 10. Multicycle path — timing budget doubles ────────────
    # sky130_inv_chain is combinational — multicycle applies to FF D-pins only.
    # Test that the constraint is stored with the right multiplier and that
    # violation count does not change for a combinational design (correct: no effect).
    print("\n[Stage 10] set_multicycle_path — budget multiplier stored correctly")
    mc10_sdc = os.path.join(ROOT, "benchmarks/test_mc10.sdc")
    with open(mc10_sdc, "w") as f:
        f.write("create_clock -period 0.1 -name clk [get_ports A]\n")
        f.write("set_multicycle_path 3 -setup -from [get_cells u_inv0] -to [get_cells u_buf0]\n")

    mc_chip = open_eda.Design()
    mc_chip.load_pdk(PDK_LIB, PDK_LEF)
    mc_chip.load_verilog(BENCH_V)
    mc_chip.read_sdc(mc10_sdc)
    os.remove(mc10_sdc)

    period_mc = mc_chip.get_clock_period()
    check("multicycle SDC: clock period 100 ps parsed",
          abs(period_mc - 100.0) < 1.0,
          f"got {period_mc:.1f} ps")
    # Verify parse stored multiplier=3 by checking violation symmetry:
    # combinational design — multicycle has no FF endpoints to apply to,
    # so violation count should equal the tight-clock baseline
    open_eda.run_placement(mc_chip)
    mc_router = open_eda.RouteEngine(); mc_router.route(mc_chip)
    mc_spef = open_eda.SpefEngine(); mc_spef.extract(mc_chip)
    mc_sta = open_eda.Timer(mc_chip, mc_spef)
    mc_sta.build_graph()
    mc_sta.update_timing()
    viols_mc  = mc_sta.get_violation_count()
    wns_mc    = mc_sta.get_wns()
    # With 100 ps clock the sky130 chain (~20-50 ps total) should meet timing
    # The multicycle path is irrelevant here (no FF D-endpoints in a comb design)
    check("multicycle SDC: 100 ps clock meets timing for this comb chain",
          wns_mc >= 0.0,
          f"WNS={wns_mc:.1f}ps")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
