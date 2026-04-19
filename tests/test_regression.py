"""
OpenEDA Regression Test Suite — Phase 0.5
Runs end-to-end flow on each benchmark and asserts key metrics.
Run from project root: python tests/test_regression.py
"""
import sys
import os
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "build", "Release"))
sys.path.insert(0, os.path.join(ROOT, "build", "Debug"))
sys.path.insert(0, os.path.join(ROOT, "build"))

PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"

results = []

def check(name, condition, msg=""):
    status = PASS if condition else FAIL
    label  = f"  [{status}] {name}"
    if msg:
        label += f"  ({msg})"
    print(label)
    results.append((name, condition))
    return condition


def run_suite():
    print("=" * 60)
    print("  OpenEDA Regression Test Suite")
    print("=" * 60)

    # ------------------------------------------------------------------
    # 1. Module import
    # ------------------------------------------------------------------
    print("\n[Stage 1] Module Import")
    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        print("  Cannot continue without the module. Build first.")
        return summarize()

    # ------------------------------------------------------------------
    # 2. Per-benchmark tests
    # ------------------------------------------------------------------
    # Only structural Verilog benchmarks — behavioral RTL (alu_32bit.v) requires
    # synthesis (Phase 5 / Yosys integration) before it can enter this flow.
    benchmarks = [
        {
            "name":     "full_adder",
            "verilog":  os.path.join(ROOT, "benchmarks", "full_adder.v"),
            "max_cells": 50,
        },
        {
            "name":     "shift_reg",
            "verilog":  os.path.join(ROOT, "benchmarks", "shift_reg.v"),
            "max_cells": 50,
        },
        {
            "name":     "adder",
            "verilog":  os.path.join(ROOT, "benchmarks", "adder.v"),
            "max_cells": 50,
        },
    ]

    for bm in benchmarks:
        name    = bm["name"]
        verilog = bm["verilog"]
        print(f"\n[Benchmark] {name}")

        if not os.path.exists(verilog):
            check(f"{name}: verilog file exists", False, verilog)
            continue
        check(f"{name}: verilog file exists", True)

        # ---- Load design ----
        try:
            chip = open_eda.Design()
            chip.load_verilog(verilog)
            cell_count = chip.get_instance_count()
            check(f"{name}: design loaded", cell_count > 0,
                  f"{cell_count} cells")
        except Exception as e:
            check(f"{name}: design loaded", False, str(e))
            continue

        # ---- Placement ----
        try:
            t0 = time.time()
            open_eda.run_placement(chip)
            elapsed = time.time() - t0
            placed = chip.get_instance_count()
            check(f"{name}: placement completed", placed > 0,
                  f"{elapsed:.1f}s")
        except Exception as e:
            check(f"{name}: placement completed", False, str(e))

        # ---- CTS ----
        try:
            cts = open_eda.CtsEngine()
            cts.run_cts(chip, "clk")
            check(f"{name}: CTS completed", True)
        except Exception as e:
            check(f"{name}: CTS completed", False, str(e))

        # ---- PDN ----
        try:
            pdn = open_eda.PdnGenerator(chip)
            pdn.run()
            check(f"{name}: PDN generated", True)
        except Exception as e:
            check(f"{name}: PDN generated", False, str(e))

        # ---- Routing ----
        try:
            t0 = time.time()
            router = open_eda.RouteEngine()
            router.route(chip)
            elapsed = time.time() - t0
            check(f"{name}: routing completed", True,
                  f"{elapsed:.1f}s")
        except Exception as e:
            check(f"{name}: routing completed", False, str(e))
            continue

        # ---- SPEF extraction ----
        try:
            spef = open_eda.SpefEngine()
            spef.extract(chip)
            spef_path = verilog.replace(".v", "_test.spef")
            spef.write_spef(spef_path, chip)
            check(f"{name}: SPEF extracted",
                  os.path.exists(spef_path) and os.path.getsize(spef_path) > 0)
            if os.path.exists(spef_path):
                os.remove(spef_path)
        except Exception as e:
            check(f"{name}: SPEF extracted", False, str(e))

        # ---- GDSII export ----
        try:
            gds_path = verilog.replace(".v", "_test.gds")
            open_eda.export_gds(gds_path, chip)
            gds_ok = os.path.exists(gds_path) and os.path.getsize(gds_path) > 0
            check(f"{name}: GDSII exported", gds_ok,
                  f"{os.path.getsize(gds_path)} bytes" if gds_ok else "empty")
            if os.path.exists(gds_path):
                os.remove(gds_path)
        except Exception as e:
            check(f"{name}: GDSII exported", False, str(e))

    # ------------------------------------------------------------------
    # 3. STA-specific checks (shift_reg — sequential design)
    # ------------------------------------------------------------------
    print("\n[Stage 3] STA Checks (shift_reg)")
    shift_v = os.path.join(ROOT, "benchmarks", "shift_reg.v")
    if os.path.exists(shift_v):
        try:
            chip = open_eda.Design()
            chip.load_verilog(shift_v)
            open_eda.run_placement(chip)

            cts = open_eda.CtsEngine()
            cts.run_cts(chip, "clk")

            pdn = open_eda.PdnGenerator(chip)
            pdn.run()

            router = open_eda.RouteEngine()
            router.route(chip)

            # SPEF for post-route STA
            spef = open_eda.SpefEngine()
            spef.extract(chip)

            # Run sign-off STA
            sta = open_eda.Timer(chip)
            sta.build_graph()
            sta.set_clock_period(1000.0)  # 1 GHz
            sta.update_timing()

            wns = sta.get_wns()
            tns = sta.get_tns()
            viol = sta.get_violation_count()

            check("STA: WNS is a finite number", wns != float('inf'),
                  f"WNS={wns:.2f}ps")
            check("STA: TNS <= 0",  tns <= 0.0, f"TNS={tns:.2f}ps")
            check("STA: violations reported as int", isinstance(viol, int),
                  f"{viol} violations")
            check("STA: zero violations at 1 GHz",  viol == 0,
                  f"{viol} violations")

        except AttributeError:
            print("  [SKIP] Timer not yet exposed in Python bindings — "
                  "add open_eda.Timer binding to run these checks")
        except Exception as e:
            check("STA: shift_reg sign-off", False, str(e))

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
