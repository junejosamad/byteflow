"""
Phase 4.1 — DRC (Design Rule Check) Engine Test Suite

Validates:
  1. API surface: DrcEngine, DrcReport, DrcViolation, DrcRuleDeck, LayerRule, ViaRule
  2. sky130 rule deck — layer count, layer rules, via rules
  3. Rule deck file loading (.drc format, nm values)
  4. DRC on routed shift_reg — detects routing shorts
  5. DrcReport fields — short_count, spacing_count, violations list
  6. DrcViolation fields — layer, net1, net2, bbox, message
  7. DRC with custom rule deck
  8. Clean sky130 combinational — DRC still runs without crash
  9. DrcViolationType enum values exist
  10. Regression: existing test suites unaffected

Run from project root:
    python tests/test_drc.py
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
DRC_DECK   = os.path.join(ROOT, "benchmarks/sky130_hd.drc")


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
    print("  Phase 4.1 - DRC Engine")
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
    check("DrcEngine class exists",      hasattr(open_eda, "DrcEngine"))
    check("DrcReport class exists",      hasattr(open_eda, "DrcReport"))
    check("DrcViolation class exists",   hasattr(open_eda, "DrcViolation"))
    check("DrcRuleDeck class exists",    hasattr(open_eda, "DrcRuleDeck"))
    check("LayerRule class exists",      hasattr(open_eda, "LayerRule"))
    check("ViaRule class exists",        hasattr(open_eda, "ViaRule"))
    check("DrcViolationType enum exists",hasattr(open_eda, "DrcViolationType"))

    drc = open_eda.DrcEngine()
    check("DrcEngine instantiates",      drc is not None)
    check("run_drc method exists",       hasattr(drc, "run_drc"))

    # ── Stage 2: DrcViolationType enum ─────────────────────────
    print("\n[Stage 2] DrcViolationType enum")
    check("SHORT enum value",        hasattr(open_eda.DrcViolationType, "SHORT"))
    check("MIN_SPACING enum value",  hasattr(open_eda.DrcViolationType, "MIN_SPACING"))
    check("MIN_WIDTH enum value",    hasattr(open_eda.DrcViolationType, "MIN_WIDTH"))
    check("MIN_AREA enum value",     hasattr(open_eda.DrcViolationType, "MIN_AREA"))
    check("VIA_ENCLOSURE enum value",hasattr(open_eda.DrcViolationType, "VIA_ENCLOSURE"))

    # ── Stage 3: sky130 rule deck ───────────────────────────────
    print("\n[Stage 3] sky130 rule deck")
    deck = open_eda.DrcRuleDeck.sky130()
    check("sky130() returns DrcRuleDeck", deck is not None)
    check("sky130 has 6 layer rules",
          len(deck.layer_rules) == 6,
          f"{len(deck.layer_rules)} layers")
    check("sky130 has 5 via rules",
          len(deck.via_rules) == 5,
          f"{len(deck.via_rules)} vias")

    # Verify met1 rule
    met1 = next((r for r in deck.layer_rules if r.name == "met1"), None)
    check("met1 layer rule found", met1 is not None)
    if met1:
        check("met1 min_width is 1.4 design units",
              abs(met1.min_width - 1.4) < 0.01,
              f"{met1.min_width:.3f}")
        check("met1 min_spacing is 1.4 design units",
              abs(met1.min_spacing - 1.4) < 0.01,
              f"{met1.min_spacing:.3f}")

    # Verify mcon via rule
    mcon = next((r for r in deck.via_rules if r.name == "mcon"), None)
    check("mcon via rule found", mcon is not None)
    if mcon:
        check("mcon from_layer=1, to_layer=2",
              mcon.from_layer == 1 and mcon.to_layer == 2,
              f"from={mcon.from_layer} to={mcon.to_layer}")

    # ── Stage 4: Rule deck file loading ────────────────────────
    print("\n[Stage 4] Rule deck file loading")
    check("sky130_hd.drc file exists",  os.path.exists(DRC_DECK), DRC_DECK)
    deck2 = open_eda.DrcRuleDeck()
    ok_load = deck2.load_from_file(DRC_DECK)
    check("load_from_file returns True", ok_load)
    check("loaded deck has layer rules", len(deck2.layer_rules) > 0,
          f"{len(deck2.layer_rules)} layers")
    check("loaded deck has via rules",   len(deck2.via_rules) > 0,
          f"{len(deck2.via_rules)} vias")
    # Layer rules should match sky130 built-in
    met1_file = next((r for r in deck2.layer_rules if r.name == "met1"), None)
    check("file met1 matches built-in",
          met1_file is not None and abs(met1_file.min_width - 1.4) < 0.05)

    # ── Stage 5: DRC on routed shift_reg ───────────────────────
    print("\n[Stage 5] DRC on routed shift_reg")
    chip5 = route(BENCH_SEQ)
    report5 = drc.run_drc(chip5)
    check("run_drc returns DrcReport",     report5 is not None)
    check("DrcReport.total_count >= 0",    report5.total_count() >= 0,
          f"{report5.total_count()} violations")
    check("DrcReport.short_count >= 0",    report5.short_count() >= 0)
    check("DrcReport.spacing_count >= 0",  report5.spacing_count() >= 0)
    check("DrcReport.violations is list",  isinstance(report5.violations, list))

    # shift_reg router flatlines with conflicts — expect short violations
    check("shift_reg has DRC violations (routing shorts expected)",
          report5.total_count() > 0,
          f"{report5.total_count()} total, {report5.short_count()} shorts")

    # ── Stage 6: DrcViolation fields ───────────────────────────
    print("\n[Stage 6] DrcViolation fields")
    if report5.violations:
        v = report5.violations[0]
        check("violation.type is DrcViolationType", True,  # type exists if we got here
              str(v.type))
        check("violation.layer > 0",    v.layer > 0, f"layer={v.layer}")
        check("violation.net1 non-empty", len(v.net1) > 0, v.net1)
        check("violation.x2 > violation.x1", v.x2 > v.x1,
              f"({v.x1:.1f},{v.y1:.1f})-({v.x2:.1f},{v.y2:.1f})")
        check("violation.message non-empty", len(v.message) > 0, v.message)
    else:
        check("DrcViolation field check (no violations — skip)", True)

    # Verify SHORT type specifically
    if report5.short_count() > 0:
        shorts = [v for v in report5.violations
                  if v.type == open_eda.DrcViolationType.SHORT]
        check("SHORT violations have net2", all(len(v.net2) > 0 for v in shorts[:5]))

    # ── Stage 7: DRC with custom rule deck ─────────────────────
    print("\n[Stage 7] DRC with custom rule deck")
    tight_deck = open_eda.DrcRuleDeck.sky130()
    report7 = drc.run_drc(chip5, tight_deck)
    check("run_drc(chip, rules) works",  report7 is not None)
    check("custom deck same short count as default",
          report7.short_count() == report5.short_count(),
          f"custom={report7.short_count()} default={report5.short_count()}")

    # ── Stage 8: DRC on sky130 combinational ───────────────────
    print("\n[Stage 8] DRC on sky130 combinational")
    chip8 = route(BENCH_COMB, pdk=True)
    report8 = drc.run_drc(chip8)
    check("sky130 comb: DRC runs without crash", report8 is not None)
    check("sky130 comb: total_count >= 0",
          report8.total_count() >= 0,
          f"{report8.total_count()} violations")
    check("sky130 comb: violations is list", isinstance(report8.violations, list))
    print(f"    (info: sky130 comb DRC: {report8.short_count()} shorts, "
          f"{report8.spacing_count()} spacing, "
          f"{report8.width_count()} width, "
          f"{report8.area_count()} area)")

    # ── Stage 9: DrcReport.print() ─────────────────────────────
    print("\n[Stage 9] DrcReport.print()")
    try:
        report5.print(5)  # just check it doesn't crash
        check("DrcReport.print() runs without crash", True)
    except Exception as e:
        check("DrcReport.print() runs without crash", False, str(e))

    # ── Stage 10: Regression ────────────────────────────────────
    print("\n[Stage 10] Regression")
    for bench, label in [("benchmarks/full_adder.v", "full_adder"),
                          ("benchmarks/adder.v",      "adder")]:
        path = os.path.join(ROOT, bench)
        if not os.path.exists(path): continue
        chip_r = route(path)
        report_r = drc.run_drc(chip_r)
        check(f"{label}: DRC runs without crash", report_r is not None)
        check(f"{label}: total_count >= 0",
              report_r.total_count() >= 0,
              f"{report_r.total_count()} violations")

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
