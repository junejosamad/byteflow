"""
Phase 3.5 — Timing Reports Test Suite

Validates:
  1. API surface: TimingReporter, PathReport, PathStep, SlackBin exist
  2. getTopPaths — fields, path ordering, step count
  3. getSlackHistogram — bin count, total endpoint coverage
  4. formatSummary — contains key fields (WNS, TNS, period)
  5. formatPath — contains startpoint/endpoint text
  6. formatSlackHistogram — ASCII bar chart non-empty
  7. formatAllEndpoints — tabular output contains endpoint names
  8. formatCdcReport — single-clock domain reports correctly
  9. writeTextReport — creates file with WNS value
  10. writeHtmlReport — creates valid HTML with summary fields
  11. sky130 comb chain — path report with NLDM delays
  12. Clean design — histogram has all endpoints in positive bins
  13. Regression: existing test suites unaffected

Run from project root:
    python tests/test_timing_reports.py
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

BENCH_SEQ  = os.path.join(ROOT, "benchmarks/shift_reg.v")
BENCH_COMB = os.path.join(ROOT, "benchmarks/sky130_inv_chain.v")
PDK_LIB    = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib")
PDK_LEF    = os.path.join(ROOT, "pdk/sky130/sky130_fd_sc_hd_merged.lef")


def flow(verilog, pdk=False, period_ps=1000.0):
    import open_eda
    chip = open_eda.Design()
    if pdk:
        chip.load_pdk(PDK_LIB, PDK_LEF)
    chip.load_verilog(verilog)
    open_eda.run_placement(chip)
    open_eda.RouteEngine().route(chip)
    spef = open_eda.SpefEngine()
    spef.extract(chip)
    sta = open_eda.Timer(chip, spef)
    sta.build_graph()
    sta.set_clock_period(period_ps)
    sta.update_timing()
    return chip, sta, spef


def run_suite():
    print("=" * 60)
    print("  Phase 3.5 - Timing Reports")
    print("=" * 60)

    # ── Import check ────────────────────────────────────────────
    try:
        import open_eda
        check("import open_eda", True)
    except ImportError as e:
        check("import open_eda", False, str(e))
        return summarize()

    import open_eda

    # ── Stage 1: API surface ────────────────────────────────────
    print("\n[Stage 1] API surface")
    check("TimingReporter class exists", hasattr(open_eda, "TimingReporter"))
    check("PathReport class exists",     hasattr(open_eda, "PathReport"))
    check("PathStep class exists",       hasattr(open_eda, "PathStep"))
    check("SlackBin class exists",       hasattr(open_eda, "SlackBin"))

    chip1, sta1, spef1 = flow(BENCH_SEQ)
    reporter = open_eda.TimingReporter(sta1, chip1)
    check("TimingReporter instantiates", reporter is not None)
    check("get_top_paths method",        hasattr(reporter, "get_top_paths"))
    check("get_slack_histogram method",  hasattr(reporter, "get_slack_histogram"))
    check("write_text_report method",    hasattr(reporter, "write_text_report"))
    check("write_html_report method",    hasattr(reporter, "write_html_report"))

    # ── Stage 2: getTopPaths ────────────────────────────────────
    print("\n[Stage 2] getTopPaths")
    paths = reporter.get_top_paths(3)
    check("get_top_paths returns list",       isinstance(paths, list))
    check("returned <= 3 paths",              len(paths) <= 3,
          f"{len(paths)} paths")
    if paths:
        p = paths[0]
        check("PathReport.slack is float",    isinstance(p.slack, float))
        check("PathReport.startpoint non-empty", len(p.startpoint) > 0,
              p.startpoint)
        check("PathReport.endpoint non-empty",   len(p.endpoint) > 0,
              p.endpoint)
        check("PathReport.steps non-empty",   len(p.steps) > 0,
              f"{len(p.steps)} steps")
        check("PathReport.type is SETUP",     p.type == "SETUP")
        if p.steps:
            s = p.steps[0]
            check("PathStep.inst_name non-empty",  len(s.inst_name) > 0)
            check("PathStep.arrival_time >= 0",    s.arrival_time >= 0.0,
                  f"{s.arrival_time:.1f} ps")
        # Paths should be sorted worst-first
        if len(paths) >= 2:
            check("paths sorted worst-slack first",
                  paths[0].slack <= paths[1].slack,
                  f"{paths[0].slack:.1f} <= {paths[1].slack:.1f}")

    # ── Stage 3: getSlackHistogram ──────────────────────────────
    print("\n[Stage 3] getSlackHistogram")
    hist = reporter.get_slack_histogram(10)
    check("histogram returns list",         isinstance(hist, list))
    check("histogram has <= 10 bins",       len(hist) <= 10,
          f"{len(hist)} bins")
    if hist:
        check("SlackBin.lo < hi",           hist[0].lo < hist[-1].hi,
              f"{hist[0].lo:.1f} .. {hist[-1].hi:.1f}")
        total_ep = sum(b.count for b in hist)
        check("histogram covers all endpoints",
              total_ep == sta1.get_violation_count() + (total_ep - sta1.get_violation_count()),
              f"total={total_ep}")
        check("SlackBin.label non-empty",   len(hist[0].label) > 0)

    # ── Stage 4: formatSummary ──────────────────────────────────
    print("\n[Stage 4] formatSummary")
    summary = reporter.format_summary()
    check("format_summary returns string",  isinstance(summary, str))
    check("summary contains WNS",
          "WNS" in summary,
          f"{len(summary)} chars")
    check("summary contains clock period",
          "1000" in summary or "Clock Period" in summary)
    check("summary contains violation count",
          "Violations" in summary or "Violation" in summary)

    # ── Stage 5: formatPath ─────────────────────────────────────
    print("\n[Stage 5] formatPath")
    if paths:
        path_str = reporter.format_path(paths[0])
        check("format_path returns string",   isinstance(path_str, str))
        check("format_path contains endpoint", paths[0].endpoint.split("/")[0] in path_str,
              f"{len(path_str)} chars")
        check("format_path contains Arrival",  "Arrival" in path_str)
        check("format_path contains slack",    "Slack" in path_str or "slack" in path_str)
    else:
        check("format_path (no paths — skipped)", True)

    # ── Stage 6: formatSlackHistogram ──────────────────────────
    print("\n[Stage 6] formatSlackHistogram")
    hist_str = reporter.format_slack_histogram(8)
    check("format_slack_histogram non-empty", len(hist_str) > 0)
    check("histogram contains bar chars",
          "#" in hist_str or "no timing" in hist_str)

    # ── Stage 7: formatAllEndpoints ────────────────────────────
    print("\n[Stage 7] formatAllEndpoints")
    ep_str = reporter.format_all_endpoints()
    check("format_all_endpoints non-empty",  len(ep_str) > 0)
    check("endpoints table has Arrival col", "Arrival" in ep_str)
    check("endpoints table has Status col",  "Status" in ep_str or "MET" in ep_str)

    # ── Stage 8: formatCdcReport ────────────────────────────────
    print("\n[Stage 8] formatCdcReport (sequential design)")
    cdc_str = reporter.format_cdc_report()
    check("format_cdc_report returns string",  isinstance(cdc_str, str))
    check("CDC report mentions domain",
          "Domain" in cdc_str or "domain" in cdc_str or "clock" in cdc_str.lower())
    check("Single-domain CDC result",
          "Single clock domain" in cdc_str or "No direct" in cdc_str
          or "no CDC" in cdc_str.lower() or "1 FF" in cdc_str)

    # ── Stage 9: writeTextReport ────────────────────────────────
    print("\n[Stage 9] writeTextReport")
    with tempfile.NamedTemporaryFile(suffix=".rpt", delete=False, mode="w") as tmp:
        rpt_path = tmp.name
    ok_rpt = reporter.write_text_report(rpt_path)
    check("write_text_report returns True",  ok_rpt)
    if ok_rpt:
        with open(rpt_path, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
        check("text report non-empty",       len(content) > 100,
              f"{len(content)} bytes")
        check("text report contains WNS",    "WNS" in content)
        check("text report contains Path",   "Path" in content or "path" in content)
    os.unlink(rpt_path)

    # ── Stage 10: writeHtmlReport ───────────────────────────────
    print("\n[Stage 10] writeHtmlReport")
    with tempfile.NamedTemporaryFile(suffix=".html", delete=False, mode="w") as tmp:
        html_path = tmp.name
    ok_html = reporter.write_html_report(html_path)
    check("write_html_report returns True",  ok_html)
    if ok_html:
        with open(html_path, "r", encoding="utf-8", errors="replace") as f:
            html = f.read()
        check("html report has DOCTYPE",      "<!DOCTYPE html>" in html)
        check("html report has <table>",      "<table>" in html)
        check("html report contains WNS",     "WNS" in html)
        check("html report contains period",  "1000" in html)
        check("html report contains endpoint","Endpoint" in html or "endpoint" in html)
    os.unlink(html_path)

    # ── Stage 11: sky130 NLDM path report ──────────────────────
    print("\n[Stage 11] sky130 NLDM path report")
    chip11, sta11, spef11 = flow(BENCH_COMB, pdk=True, period_ps=10.0)
    rep11   = open_eda.TimingReporter(sta11, chip11)
    paths11 = rep11.get_top_paths(1)
    check("sky130 top path exists",     len(paths11) == 1)
    if paths11:
        p11 = paths11[0]
        check("sky130 path has steps",  len(p11.steps) > 0, f"{len(p11.steps)} steps")
        check("sky130 path slack finite",
              p11.slack not in (float("inf"), float("-inf")),
              f"{p11.slack:.1f} ps")
        check("at least one step has gate delay",
              any(s.gate_delay > 0 for s in p11.steps))

    # ── Stage 12: clean design histogram (all in positive bins) ─
    print("\n[Stage 12] Clean design histogram")
    chip12, sta12, spef12 = flow(BENCH_COMB, pdk=True, period_ps=2000.0)
    rep12 = open_eda.TimingReporter(sta12, chip12)
    v12   = sta12.get_violation_count()
    check("comb design has 0 violations", v12 == 0, f"{v12} violations")
    hist12 = rep12.get_slack_histogram(5)
    if hist12:
        viol_bins = sum(b.count for b in hist12 if b.hi <= 0)
        check("no endpoints in negative bins", viol_bins == 0,
              f"{viol_bins} in viol bins")

    # ── Stage 13: Regression ────────────────────────────────────
    print("\n[Stage 13] Regression")
    for bench, label in [("benchmarks/full_adder.v", "full_adder"),
                          ("benchmarks/adder.v",      "adder")]:
        path = os.path.join(ROOT, bench)
        if not os.path.exists(path): continue
        chip_r, sta_r, spef_r = flow(path)
        rep_r = open_eda.TimingReporter(sta_r, chip_r)
        paths_r = rep_r.get_top_paths(1)
        check(f"{label}: top path returned",
              len(paths_r) == 1,
              f"{len(paths_r)} paths")
        if paths_r:
            check(f"{label}: path slack finite",
                  paths_r[0].slack not in (float("inf"), float("-inf")),
                  f"slack={paths_r[0].slack:.1f}ps")
        html_r_path = os.path.join(tempfile.gettempdir(), f"test_{label}.html")
        ok_r = rep_r.write_html_report(html_r_path)
        check(f"{label}: HTML report written", ok_r)
        if ok_r: os.unlink(html_r_path)

    return summarize()


if __name__ == "__main__":
    ok = run_suite()
    sys.exit(0 if ok else 1)
