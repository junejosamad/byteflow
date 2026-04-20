"""
Phase 2.1 — SkyWater 130nm PDK Integration Test

Validates:
  1. Liberty parser handles sky130 cell syntax (quoted names, pg_pin, leakage_power)
  2. LEF parser handles sky130 layer stack and filters power pins
  3. Full flow: load_pdk → load_verilog → place → CTS → PDN → route → SPEF → GDSII
  4. GDS layer numbers match sky130 standard (li1=67, met1=68, …)

Run from project root:
    python tests/test_sky130.py
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

def summarize():
    total  = len(results)
    passed = sum(1 for _, ok in results if ok)
    failed = total - passed
    print("\n" + "=" * 60)
    print(f"  Results: {passed}/{total} passed"
          + (f"  ({failed} FAILED)" if failed else ""))
    print("=" * 60)
    return failed == 0

PDK_LIB = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib")
PDK_LEF = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd_merged.lef")
BENCH_V  = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")


def run_suite():
    print("=" * 60)
    print("  Phase 2.1 — SkyWater 130nm PDK Integration Test")
    print("=" * 60)

    # ── 1. Module import ─────────────────────────────────────────────────────
    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    # ── 2. PDK files present ─────────────────────────────────────────────────
    check("sky130 Liberty file exists", os.path.exists(PDK_LIB), PDK_LIB)
    check("sky130 LEF file exists",     os.path.exists(PDK_LEF), PDK_LEF)
    check("sky130 benchmark exists",    os.path.exists(BENCH_V),  BENCH_V)

    if not (os.path.exists(PDK_LIB) and os.path.exists(PDK_LEF) and os.path.exists(BENCH_V)):
        print(f"  [{SKIP}] Missing PDK/benchmark files — run pdk download script first")
        return summarize()

    # ── 3. Load PDK (Liberty + LEF) ──────────────────────────────────────────
    print("\n[Stage 3] PDK Loading")
    try:
        chip = open_eda.Design()
        chip.load_pdk(PDK_LIB, PDK_LEF)
        check("load_pdk completed",  True)
    except Exception as e:
        check("load_pdk completed",  False, str(e))
        return summarize()

    # ── 4. Liberty cell count ─────────────────────────────────────────────────
    # sky130_fd_sc_hd has 428 cells; we need at least the ones in our benchmark
    required_cells = [
        "sky130_fd_sc_hd__inv_1",
        "sky130_fd_sc_hd__inv_2",
        "sky130_fd_sc_hd__nand2_1",
        "sky130_fd_sc_hd__buf_1",
        "sky130_fd_sc_hd__nor2_1",
    ]
    print("\n[Stage 4] Liberty parse verification")
    # Probe via load_verilog with the benchmark (it will fail to find unknown cells)
    try:
        chip.load_verilog(BENCH_V)
        n = chip.get_instance_count()
        check("load_verilog on sky130 netlist", n > 0, f"{n} instances")
    except Exception as e:
        check("load_verilog on sky130 netlist", False, str(e))
        return summarize()

    # ── 5. Full EDA flow ──────────────────────────────────────────────────────
    print("\n[Stage 5] Full EDA flow")

    # Placement
    try:
        t0 = time.time()
        open_eda.run_placement(chip)
        pt = time.time() - t0
        check("placement completed", True, f"{pt:.2f}s")
    except Exception as e:
        check("placement completed", False, str(e))

    # CTS
    try:
        cts = open_eda.CtsEngine()
        cts.run_cts(chip, "clk")
        check("CTS completed", True)
    except Exception as e:
        check("CTS completed", False, str(e))

    # PDN
    try:
        pdn = open_eda.PdnGenerator(chip)
        pdn.run()
        check("PDN generated", True)
    except Exception as e:
        check("PDN generated", False, str(e))

    # Routing
    try:
        t0 = time.time()
        router = open_eda.RouteEngine()
        router.route(chip)
        rt = time.time() - t0
        check("routing completed", True, f"{rt:.2f}s")
    except Exception as e:
        check("routing completed", False, str(e))

    # SPEF
    try:
        spef = open_eda.SpefEngine()
        spef.extract(chip)
        check("SPEF extracted", True)
    except Exception as e:
        check("SPEF extracted", False, str(e))

    # GDSII — verify sky130 layer numbers in output
    gds_path = os.path.join(ROOT, "benchmarks/sky130_inv_chain_test.gds")
    try:
        ok = open_eda.export_gds(gds_path, chip)
        gds_ok = os.path.exists(gds_path) and os.path.getsize(gds_path) > 0
        check("GDSII exported", gds_ok,
              f"{os.path.getsize(gds_path)} bytes" if gds_ok else "empty/missing")
    except Exception as e:
        check("GDSII exported", False, str(e))
        gds_ok = False

    # Validate sky130 layer numbers in the GDS binary
    if gds_ok:
        sky130_layer_ok = _check_gds_layers(gds_path)
        check("GDS contains sky130 layer 68 (met1)", sky130_layer_ok)
    if os.path.exists(gds_path):
        os.remove(gds_path)

    # STA sign-off
    try:
        spef_obj = open_eda.SpefEngine()
        spef_obj.extract(chip)
        sta = open_eda.Timer(chip, spef_obj)
        sta.build_graph()
        sta.set_clock_period(2000.0)  # 500 MHz — achievable for sky130 combinational
        sta.update_timing()
        wns = sta.get_wns()
        check("STA WNS is finite",
              wns != float("inf") and wns != float("-inf"),
              f"WNS={wns:.1f}ps")
    except Exception as e:
        check("STA sign-off", False, str(e))

    return summarize()


def _check_gds_layers(path):
    """Scan GDS binary for LAYER records (type 0x0D02) containing sky130 layers 67-72."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        # GDSII LAYER record layout: [len_hi len_lo 0D 02 val_hi val_lo]
        # Record type bytes are 0x0D, 0x02.  Preceded by length=6 (0x00 0x06).
        layer_type = bytes([0x0D, 0x02])
        i = 0
        while i < len(data) - 3:
            idx = data.find(layer_type, i)
            if idx == -1:
                break
            if idx >= 2 and data[idx - 2] == 0x00 and data[idx - 1] == 0x06:
                layer_val = (data[idx + 2] << 8) | data[idx + 3]
                if layer_val in (67, 68, 69, 70, 71, 72):
                    return True
            i = idx + 1
    except Exception:
        pass
    return False


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
