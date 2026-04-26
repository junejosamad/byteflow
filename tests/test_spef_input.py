"""
Phase 2.5 -- SPEF Input Parser Test

Validates:
  1. SpefEngine.write_spef() produces a parseable SPEF file
  2. SpefEngine.read_spef() loads the file back into the engine
  3. Per-net wire delays and capacitances survive a write/read round-trip
  4. STA using back-annotated SPEF produces consistent WNS with extracted SPEF
  5. read_spef returns False for non-existent file
  6. New query helpers (get_wire_delay, get_net_cap, get_extracted_net_count)
  7. Multiple read_spef calls accumulate without crashing

Run from project root:
    python tests/test_spef_input.py
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


def load_design():
    import open_eda
    chip = open_eda.Design()
    chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(BENCH_V)
    open_eda.run_placement(chip)
    r = open_eda.RouteEngine()
    r.route(chip)
    return chip


def run_suite():
    print("=" * 60)
    print("  Phase 2.5 -- SPEF Input Parser Test")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    for label, path in [("sky130 Liberty", PDK_LIB),
                        ("sky130 LEF",     PDK_LEF),
                        ("benchmark .v",   BENCH_V)]:
        if not check(f"{label} file exists", os.path.exists(path), path):
            return summarize()

    # -- load design --
    print("\n[Setup] Loading and routing design...")
    try:
        chip = load_design()
        check("load_design + route", True)
    except Exception as e:
        check("load_design + route", False, str(e))
        return summarize()

    # ------------------------------------------------------------------ Stage 1
    print("\n[Stage 1] write_spef produces a non-empty file")
    spef_extracted = open_eda.SpefEngine()
    spef_extracted.extract(chip)
    net_count_extracted = spef_extracted.get_extracted_net_count()
    check("extract() populates nets", net_count_extracted > 0,
          f"nets={net_count_extracted}")

    with tempfile.NamedTemporaryFile(suffix=".spef", delete=False) as tmp:
        spef_path = tmp.name

    try:
        spef_extracted.write_spef(spef_path, chip)
        size = os.path.getsize(spef_path)
        check("write_spef file non-empty", size > 0, f"size={size} bytes")

        # ---------------------------------------------------------------- Stage 2
        print("\n[Stage 2] read_spef loads the file back")
        spef_loaded = open_eda.SpefEngine()
        ok = spef_loaded.read_spef(spef_path)
        check("read_spef returns True", ok)
        net_count_loaded = spef_loaded.get_extracted_net_count()
        check("read_spef loads all nets",
              net_count_loaded == net_count_extracted,
              f"loaded={net_count_loaded}  extracted={net_count_extracted}")

        # ---------------------------------------------------------------- Stage 3
        print("\n[Stage 3] Per-net round-trip consistency")
        # Grab one net name that exists in both engines
        sample_net = None
        for net in chip.get_nets() if hasattr(chip, 'get_nets') else []:
            sample_net = net; break

        # Fallback: known net names from sky130_inv_chain
        for candidate in ["n1", "n2", "n3", "n4", "n5"]:
            d_ext = spef_extracted.get_wire_delay(candidate)
            if d_ext > 0:
                d_lod = spef_loaded.get_wire_delay(candidate)
                check(f"wire_delay round-trip ({candidate})",
                      abs(d_ext - d_lod) < 0.01,
                      f"extracted={d_ext:.4f}ps  loaded={d_lod:.4f}ps")
                c_ext = spef_extracted.get_net_cap(candidate)
                c_lod = spef_loaded.get_net_cap(candidate)
                check(f"net_cap round-trip ({candidate})",
                      abs(c_ext - c_lod) < 0.01,
                      f"extracted={c_ext:.4f}fF  loaded={c_lod:.4f}fF")
                break

        # ---------------------------------------------------------------- Stage 4
        print("\n[Stage 4] STA with back-annotated SPEF gives consistent WNS")
        def run_sta(spef_eng):
            import open_eda as oe
            t = oe.Timer(chip, spef_eng)
            t.build_graph()
            t.set_clock_period(2000.0)
            t.update_timing()
            return t.get_wns()

        wns_extracted = run_sta(spef_extracted)
        wns_loaded    = run_sta(spef_loaded)
        check("WNS consistent across extract vs read_spef",
              abs(wns_extracted - wns_loaded) < 1.0,
              f"extracted={wns_extracted:.1f}ps  loaded={wns_loaded:.1f}ps")

        # ---------------------------------------------------------------- Stage 5
        print("\n[Stage 5] Missing file returns False")
        bad = open_eda.SpefEngine()
        check("read_spef missing file returns False",
              bad.read_spef("/nonexistent/path/to.spef") == False)
        check("no nets after failed read",
              bad.get_extracted_net_count() == 0)

        # ---------------------------------------------------------------- Stage 6
        print("\n[Stage 6] get_wire_delay / get_net_cap helpers")
        check("get_wire_delay unknown net returns 0",
              spef_loaded.get_wire_delay("__no_such_net__") == 0.0)
        check("get_net_cap unknown net returns 0",
              spef_loaded.get_net_cap("__no_such_net__") == 0.0)
        check("get_extracted_net_count > 0 after read",
              spef_loaded.get_extracted_net_count() > 0)

        # ---------------------------------------------------------------- Stage 7
        print("\n[Stage 7] Second read_spef call accumulates without crash")
        try:
            spef_loaded.read_spef(spef_path)
            check("second read_spef does not crash", True)
            check("net count still correct after re-read",
                  spef_loaded.get_extracted_net_count() >= net_count_extracted)
        except Exception as e:
            check("second read_spef does not crash", False, str(e))

    finally:
        os.unlink(spef_path)

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
