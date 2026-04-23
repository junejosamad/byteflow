"""
DRC Extended Tests — covers violation types not reachable with sky130 defaults

The sky130 rule deck has minWidth=140nm, minSpacing=140nm — far below our
2μm wire width. This file uses a synthetically tight rule deck (written to a
temp .drc file) to exercise MIN_WIDTH, MIN_SPACING, and MIN_AREA code paths
that default runs never reach.

Stages:
  1. Tight custom rule deck — load, verify fields
  2. MIN_WIDTH violations (tight minWidth > actual wire width)
  3. MIN_SPACING violations (tight minSpacing > actual inter-wire gap)
  4. MIN_AREA violations (tight minArea > actual segment area)
  5. DRC on unrouted design — no routing geometry, 0 violations
  6. Violation fields — layer, nets, bbox, message content
  7. Violation counts are consistent with violation list length
  8. Regression — sky130 defaults still 0 spacing/width/area violations

Run from project root:
    python tests/test_drc_extended.py
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

BENCH = os.path.join(ROOT, "benchmarks/shift_reg.v")

# ── Tight rule deck values (in nm) ──────────────────────────────────────────
# Our wires are 2*HALF_WIDTH = 20 design units = 2000nm wide.
# MIN_WIDTH  fires when narrow_dim  < minWidth  → use 2500nm (> 2000nm)
# MIN_AREA   fires when area        < minArea   → use 5e9 nm² (> any short seg)
# MIN_SPACING fires when gap between facing wires < minSpacing → use 50000nm
#
# Values stored in nm in the .drc file; the parser converts ÷ 100 to design units.

TIGHT_DRC_CONTENT = """\
# Tight DRC rule deck for test purposes — values chosen to force all violation types
# min_width  = 2500 nm → 25 design units  (actual wire width = 20 units → MIN_WIDTH)
# min_spacing= 50000 nm → 500 design units (>> coreSize → MIN_SPACING on parallel wires)
# min_area   = 5000000000 nm² → 500000 du² (>> any actual wire area → MIN_AREA)
layer 1 li1   min_width=2500  min_spacing=50000  min_area=5000000000
layer 2 met1  min_width=2500  min_spacing=50000  min_area=5000000000
layer 3 met2  min_width=2500  min_spacing=50000  min_area=5000000000
layer 4 met3  min_width=2500  min_spacing=50000  min_area=5000000000
layer 5 met4  min_width=2500  min_spacing=50000  min_area=5000000000
layer 6 met5  min_width=2500  min_spacing=50000  min_area=5000000000
via 1 2 mcon  enclosure=60   via_size=170
"""


def write_tight_deck():
    """Write tight .drc to a temp file; return path."""
    tmp = tempfile.NamedTemporaryFile(suffix=".drc", delete=False, mode="w", encoding="utf-8")
    tmp.write(TIGHT_DRC_CONTENT)
    tmp.close()
    return tmp.name


def place_and_route(verilog):
    import open_eda
    chip = open_eda.Design()
    chip.load_verilog(verilog)
    open_eda.run_placement(chip)
    open_eda.RouteEngine().route(chip)
    return chip


def run_suite():
    print("=" * 60)
    print("  DRC Extended — MIN_WIDTH / MIN_SPACING / MIN_AREA")
    print("=" * 60)

    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    tight_path = write_tight_deck()
    drc = open_eda.DrcEngine()

    # ── Stage 1: Tight rule deck loading ───────────────────────
    print("\n[Stage 1] Tight custom rule deck")
    deck = open_eda.DrcRuleDeck()
    ok = deck.load_from_file(tight_path)
    check("load_from_file returns True",    ok)
    check("tight deck has 6 layers",        len(deck.layer_rules) == 6,
          f"{len(deck.layer_rules)}")
    met1 = next((r for r in deck.layer_rules if r.name == "met1"), None)
    check("met1 loaded",                    met1 is not None)
    if met1:
        check("met1 min_width  == 25 du",   abs(met1.min_width   -   25.0) < 0.1,
              f"{met1.min_width:.1f}")
        check("met1 min_spacing== 500 du",  abs(met1.min_spacing - 500.0) < 1.0,
              f"{met1.min_spacing:.1f}")
        # min_area = 5e9 nm² → 5e9 / (100*100) = 500000 design units²
        check("met1 min_area   == 500000 du²",
              abs(met1.min_area - 500000.0) < 1.0,
              f"{met1.min_area:.0f}")

    # ── Stage 2: MIN_WIDTH violations ──────────────────────────
    print("\n[Stage 2] MIN_WIDTH violations (tight minWidth=25 du > wire 20 du)")
    chip2 = place_and_route(BENCH)
    report_tight = drc.run_drc(chip2, deck)

    check("DRC with tight deck returns report",   report_tight is not None)
    check("MIN_WIDTH violations detected",
          report_tight.width_count() > 0,
          f"{report_tight.width_count()} width violations")

    # All width violations should reference a specific layer
    width_viols = [v for v in report_tight.violations
                   if v.type == open_eda.DrcViolationType.MIN_WIDTH]
    if width_viols:
        check("width violation has layer > 0",    all(v.layer > 0 for v in width_viols[:5]))
        check("width violation has net1 non-empty",
              all(len(v.net1) > 0 for v in width_viols[:5]))
        check("width violation message contains 'width'",
              all("width" in v.message for v in width_viols[:5]))
        # Bbox makes sense: x2 > x1, y2 > y1
        check("width violation bbox valid",
              all(v.x2 > v.x1 and v.y2 > v.y1 for v in width_viols[:5]))

    # ── Stage 3: MIN_SPACING violations ─────────────────────────
    print("\n[Stage 3] MIN_SPACING violations (tight minSpacing=500 du >> coreSize)")
    check("MIN_SPACING violations detected",
          report_tight.spacing_count() > 0,
          f"{report_tight.spacing_count()} spacing violations")

    spacing_viols = [v for v in report_tight.violations
                     if v.type == open_eda.DrcViolationType.MIN_SPACING]
    if spacing_viols:
        check("spacing violation has two different nets",
              all(v.net1 != v.net2 for v in spacing_viols[:5]),
              f"e.g. {spacing_viols[0].net1} vs {spacing_viols[0].net2}")
        check("spacing violation message contains 'gap' or 'rule'",
              all("gap" in v.message or "rule" in v.message for v in spacing_viols[:5]))
        check("spacing violation layer > 0",    all(v.layer > 0 for v in spacing_viols[:5]))

    # ── Stage 4: MIN_AREA violations ────────────────────────────
    print("\n[Stage 4] MIN_AREA violations (tight minArea=500000 du²)")
    check("MIN_AREA violations detected",
          report_tight.area_count() > 0,
          f"{report_tight.area_count()} area violations")

    area_viols = [v for v in report_tight.violations
                  if v.type == open_eda.DrcViolationType.MIN_AREA]
    if area_viols:
        check("area violation has layer > 0",    all(v.layer > 0 for v in area_viols[:5]))
        check("area violation has net1 non-empty",
              all(len(v.net1) > 0 for v in area_viols[:5]))
        check("area violation message contains 'area'",
              all("area" in v.message for v in area_viols[:5]))

    # ── Stage 5: DRC on pre-route design (0 routing geometry) ──
    print("\n[Stage 5] DRC on unrouted design — 0 violations")
    chip5 = open_eda.Design()
    chip5.load_verilog(BENCH)
    open_eda.run_placement(chip5)
    # Deliberately skip RouteEngine().route(chip5)
    report5 = drc.run_drc(chip5)
    check("unrouted design: DRC runs without crash",  report5 is not None)
    check("unrouted design: 0 total violations",
          report5.total_count() == 0,
          f"{report5.total_count()} violations")
    check("unrouted design: 0 shorts",   report5.short_count()   == 0)
    check("unrouted design: 0 spacing",  report5.spacing_count() == 0)
    check("unrouted design: 0 width",    report5.width_count()   == 0)
    check("unrouted design: 0 area",     report5.area_count()    == 0)

    # ── Stage 6: Violation count consistency ────────────────────
    print("\n[Stage 6] Violation count consistency")
    total_from_list = len(report_tight.violations)
    check("total_count() == len(violations)",
          report_tight.total_count() == total_from_list,
          f"total={report_tight.total_count()} list={total_from_list}")
    sum_by_type = (report_tight.short_count()
                 + report_tight.spacing_count()
                 + report_tight.width_count()
                 + report_tight.area_count())
    check("sum of type counts == total",
          sum_by_type == report_tight.total_count(),
          f"sum={sum_by_type} total={report_tight.total_count()}")
    check("tight deck has all four violation types",
          report_tight.short_count()   > 0 and
          report_tight.spacing_count() > 0 and
          report_tight.width_count()   > 0 and
          report_tight.area_count()    > 0)

    # ── Stage 7: print() with mixed types ───────────────────────
    print("\n[Stage 7] print() with mixed violation types")
    try:
        report_tight.print(10)
        check("DrcReport.print() handles mixed types", True)
    except Exception as e:
        check("DrcReport.print() handles mixed types", False, str(e))

    # ── Stage 8: Regression — sky130 defaults have 0 width/spacing/area ──
    print("\n[Stage 8] Regression — sky130 defaults produce only SHORTs")
    report_default = drc.run_drc(chip2)
    check("default deck: 0 MIN_WIDTH violations",   report_default.width_count()   == 0,
          f"{report_default.width_count()}")
    check("default deck: 0 MIN_SPACING violations", report_default.spacing_count() == 0,
          f"{report_default.spacing_count()}")
    check("default deck: 0 MIN_AREA violations",    report_default.area_count()    == 0,
          f"{report_default.area_count()}")
    check("default deck: shorts still detected",    report_default.short_count()   > 0,
          f"{report_default.short_count()} shorts")

    os.unlink(tight_path)
    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
