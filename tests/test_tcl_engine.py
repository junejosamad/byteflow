"""
Phase 2.7 -- TCL Engine Test

Validates:
  1. TclEngine constructs and accepts a Design pointer
  2. puts command appends to output
  3. set command stores variables; $var substitution works
  4. Unknown command returns False and sets error string
  5. read_verilog command loads a netlist into the Design
  6. place_design command runs placement
  7. report_timing command runs STA and includes WNS in output
  8. check_drc runs DRC and returns output
  9. check_lvs runs LVS and returns output
 10. run_script executes a .tcl file end-to-end
 11. help command lists all commands
 12. clear_output resets accumulated output
 13. Sequential commands: read_liberty -> read_lef -> read_verilog -> place_design

Run from project root:
    python tests/test_tcl_engine.py
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
    label = f"  [{status}] {name}"
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
BENCH_V = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")
BENCH_TCL = os.path.join(ROOT, "benchmarks/test_flow.tcl")


def run_suite():
    print("=" * 60)
    print("  Phase 2.7 -- TCL Engine Test")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    for label, path in [("sky130 Liberty", PDK_LIB),
                        ("sky130 LEF",     PDK_LEF),
                        ("benchmark .v",   BENCH_V),
                        ("benchmark .tcl", BENCH_TCL)]:
        if not check(f"{label} file exists", os.path.exists(path), path):
            return summarize()

    # ------------------------------------------------------------------ Stage 1
    print("\n[Stage 1] TclEngine constructs bound to a Design")
    chip = open_eda.Design()
    try:
        tcl = open_eda.TclEngine(chip)
        check("TclEngine() constructs", True)
    except Exception as e:
        check("TclEngine() constructs", False, str(e))
        return summarize()

    # ------------------------------------------------------------------ Stage 2
    print("\n[Stage 2] puts appends to output")
    tcl.clear_output()
    ok = tcl.run_command('puts "Hello from TCL"')
    check("puts returns True", ok)
    check("puts adds to output", "Hello from TCL" in tcl.get_output())

    # ------------------------------------------------------------------ Stage 3
    print("\n[Stage 3] set + variable substitution")
    tcl.clear_output()
    tcl.run_command("set myvar foobar")
    tcl.run_command('puts $myvar')
    check("set + $var substitution", "foobar" in tcl.get_output())

    # ------------------------------------------------------------------ Stage 4
    print("\n[Stage 4] Unknown command returns False and sets error")
    ok = tcl.run_command("not_a_real_command arg1 arg2")
    check("unknown command returns False", not ok)
    check("unknown command sets error string",
          len(tcl.get_error()) > 0, f"error='{tcl.get_error()}'")

    # ------------------------------------------------------------------ Stage 5
    print("\n[Stage 5] read_liberty + read_lef + read_verilog")
    chip2 = open_eda.Design()
    tcl2 = open_eda.TclEngine(chip2)

    ok_lib = tcl2.run_command(f'read_liberty "{PDK_LIB}"')
    check("read_liberty returns True", ok_lib)
    check("read_liberty output mentions cells", "cells" in tcl2.get_output())

    ok_lef = tcl2.run_command(f'read_lef "{PDK_LEF}"')
    check("read_lef returns True", ok_lef)

    ok_v = tcl2.run_command(f'read_verilog "{BENCH_V}"')
    check("read_verilog returns True", ok_v)
    check("read_verilog output mentions instances",
          "instances" in tcl2.get_output())
    check("design has instances after read_verilog",
          chip2.get_instance_count() > 0,
          f"count={chip2.get_instance_count()}")

    # ------------------------------------------------------------------ Stage 6
    print("\n[Stage 6] place_design runs placement")
    ok_place = tcl2.run_command("place_design")
    check("place_design returns True", ok_place)
    check("place_design output mentions placed",
          "placed" in tcl2.get_output().lower())

    # ------------------------------------------------------------------ Stage 7
    print("\n[Stage 7] report_timing runs STA")
    tcl2.clear_output()
    ok_rpt = tcl2.run_command("report_timing -period 2000")
    check("report_timing returns True", ok_rpt)
    out = tcl2.get_output()
    check("report_timing output contains WNS", "WNS" in out)
    check("report_timing output contains period", "2000" in out)

    # ------------------------------------------------------------------ Stage 8
    print("\n[Stage 8] check_drc runs without crash")
    tcl2.clear_output()
    ok_drc = tcl2.run_command("check_drc")
    check("check_drc returns True", ok_drc)
    check("check_drc output mentions shorts", "shorts" in tcl2.get_output().lower()
          or "drc" in tcl2.get_output().lower())

    # ------------------------------------------------------------------ Stage 9
    print("\n[Stage 9] check_lvs runs without crash")
    tcl2.clear_output()
    ok_lvs = tcl2.run_command("check_lvs")
    check("check_lvs returns True", ok_lvs)
    check("check_lvs output mentions clean or mismatch",
          "clean" in tcl2.get_output().lower()
          or "mismatch" in tcl2.get_output().lower())

    # ----------------------------------------------------------------- Stage 10
    print("\n[Stage 10] run_script executes benchmarks/test_flow.tcl")
    chip3 = open_eda.Design()
    tcl3 = open_eda.TclEngine(chip3)
    try:
        ok_script = tcl3.run_script(BENCH_TCL)
        check("run_script completes without exception", True)
        # Script runs several commands; output should be non-empty
        check("run_script produces output", len(tcl3.get_output()) > 20)
        check("run_script output contains 'Flow complete'",
              "Flow complete" in tcl3.get_output())
    except Exception as e:
        check("run_script completes without exception", False, str(e))

    # ----------------------------------------------------------------- Stage 11
    print("\n[Stage 11] help lists available commands")
    tcl_help = open_eda.TclEngine(open_eda.Design())
    tcl_help.run_command("help")
    out = tcl_help.get_output()
    for cmd in ["read_verilog", "place_design", "route_design",
                "write_gds", "report_timing", "check_drc"]:
        check(f"help lists '{cmd}'", cmd in out)

    # ----------------------------------------------------------------- Stage 12
    print("\n[Stage 12] clear_output resets buffers")
    tcl_c = open_eda.TclEngine(open_eda.Design())
    tcl_c.run_command('puts "something"')
    check("output non-empty before clear", len(tcl_c.get_output()) > 0)
    tcl_c.clear_output()
    check("output empty after clear_output", tcl_c.get_output() == "")
    check("error empty after clear_output", tcl_c.get_error() == "")

    # ----------------------------------------------------------------- Stage 13
    print("\n[Stage 13] Inline script via run_command sequence")
    chip4 = open_eda.Design()
    tcl4 = open_eda.TclEngine(chip4)
    cmds = [
        f'read_liberty "{PDK_LIB}"',
        f'read_lef "{PDK_LEF}"',
        f'read_verilog "{BENCH_V}"',
        'place_design',
        'route_design',
    ]
    all_ok = all(tcl4.run_command(c) for c in cmds)
    check("full inline flow (liberty->lef->v->place->route) succeeds", all_ok)
    check("design has instances after full inline flow",
          chip4.get_instance_count() > 0)

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
