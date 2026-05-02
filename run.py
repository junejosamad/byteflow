"""
OpenEDA — Full RTL-to-GDSII Flow
Usage: python run.py
"""
import sys, os

sys.path.insert(0, './build/Release')
import open_eda

# ─── CONFIG ───────────────────────────────────────────────────────────────────
VERILOG     = 'benchmarks/full_adder.v'
CLOCK_NET   = 'clk'
CLOCK_PS    = 1000.0          # 1 GHz target
OUTPUT_GDS  = 'output.gds'
OUTPUT_SPEF = 'output.spef'
OUTPUT_RPT  = 'output_timing.rpt'
OUTPUT_HTML = 'output_timing.html'

# Set to True + point at your PDK files to use SkyWater 130nm instead of simple.lib
USE_SKY130  = False
PDK_LIB     = 'pdk/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib'
PDK_LEF     = 'pdk/sky130/sky130_fd_sc_hd_merged.lef'
# ──────────────────────────────────────────────────────────────────────────────

def hr(title):
    print(f"\n{'-' * 60}")
    print(f"  {title}")
    print('-' * 60)

# ─── 1. LOAD DESIGN ───────────────────────────────────────────────────────────
hr("STEP 1 — Load Design")
chip = open_eda.Design()

if USE_SKY130:
    print(f"  PDK : {PDK_LIB}")
    chip.load_pdk(PDK_LIB, PDK_LEF)

print(f"  Netlist: {VERILOG}")
chip.load_verilog(VERILOG)
print(f"  Instances: {chip.get_instance_count()}")

# ─── 2. PLACEMENT ─────────────────────────────────────────────────────────────
hr("STEP 2 — Placement")
open_eda.run_placement(chip)
print(f"  Placed {chip.get_instance_count()} cells")

# ─── 3. CLOCK TREE SYNTHESIS ──────────────────────────────────────────────────
hr("STEP 3 — Clock Tree Synthesis")
cts = open_eda.CtsEngine()
cts.run_cts(chip, CLOCK_NET)

# ─── 4. POWER DISTRIBUTION NETWORK ───────────────────────────────────────────
hr("STEP 4 — PDN Generation")
pdn = open_eda.PdnGenerator(chip)
pdn.run()

# ─── 5. ROUTING ───────────────────────────────────────────────────────────────
hr("STEP 5 — Routing")
router = open_eda.RouteEngine()
router.route(chip)

# ─── 6. RC PARASITIC EXTRACTION ───────────────────────────────────────────────
hr("STEP 6 — RC Parasitic Extraction (SPEF)")
spef = open_eda.SpefEngine()
spef.extract(chip)
spef.write_spef(OUTPUT_SPEF, chip)
print(f"  SPEF written: {OUTPUT_SPEF}  ({os.path.getsize(OUTPUT_SPEF)} bytes)")

# ─── 7. STATIC TIMING ANALYSIS ────────────────────────────────────────────────
hr("STEP 7 — Static Timing Analysis")
sta = open_eda.Timer(chip, spef)
sta.build_graph()
sta.set_clock_period(CLOCK_PS)
sta.update_timing()

wns  = sta.get_wns()
tns  = sta.get_tns()
viol = sta.get_violation_count()
hold_wns  = sta.get_hold_wns()
hold_viol = sta.get_hold_violation_count()

print(f"  Clock period : {CLOCK_PS:.0f} ps")
print(f"  Setup WNS    : {wns:.2f} ps  ({'PASS' if wns >= 0 else 'FAIL'})")
print(f"  Setup TNS    : {tns:.2f} ps")
print(f"  Setup viol.  : {viol}")
print(f"  Hold WNS     : {hold_wns:.2f} ps  ({'PASS' if hold_wns >= 0 else 'FAIL'})")
print(f"  Hold viol.   : {hold_viol}")

# ─── 8. ECO TIMING CLOSURE (if needed) ────────────────────────────────────────
if viol > 0 or hold_viol > 0:
    hr("STEP 8 — ECO Timing Closure")
    eco = open_eda.EcoEngine()
    result = eco.run_timing_closure(chip, sta)
    print(f"  Setup fixed    : {result.setup_fixed}")
    print(f"  Hold fixed     : {result.hold_fixed}")
    print(f"  Iterations     : {result.iterations}")
    print(f"  Post-ECO Setup WNS : {result.final_setup_wns:.2f} ps")
    print(f"  Post-ECO Hold  WNS : {result.final_hold_wns:.2f} ps")
    sta.update_timing()
else:
    print("\n  Timing clean — ECO not needed.")

# ─── 9. TIMING REPORTS ────────────────────────────────────────────────────────
hr("STEP 9 — Timing Reports")
reporter = open_eda.TimingReporter(sta, chip)
reporter.write_text_report(OUTPUT_RPT)
reporter.write_html_report(OUTPUT_HTML)
print(f"  Text report : {OUTPUT_RPT}")
print(f"  HTML report : {OUTPUT_HTML}")

# ─── 10. DRC ──────────────────────────────────────────────────────────────────
hr("STEP 10 — Design Rule Check (DRC)")
drc = open_eda.DrcEngine()
drc_report = drc.run_drc(chip)
print(f"  Shorts          : {drc_report.short_count()}")
print(f"  Spacing viol.   : {drc_report.spacing_count()}")
print(f"  Width viol.     : {drc_report.width_count()}")
total_drc = drc_report.total_count()
print(f"  Total violations: {total_drc}  ({'CLEAN' if total_drc == 0 else 'VIOLATIONS FOUND'})")

# ─── 11. LVS ──────────────────────────────────────────────────────────────────
hr("STEP 11 — Layout vs. Schematic (LVS)")
lvs = open_eda.LvsEngine()
lvs_report = lvs.run_lvs(chip)
print(f"  LVS clean: {lvs_report.clean()}  ({len(lvs_report.mismatches)} mismatches)")
if not lvs_report.clean():
    for m in lvs_report.mismatches[:5]:
        print(f"    - {m.message}")

# ─── 12. GDSII EXPORT ─────────────────────────────────────────────────────────
hr("STEP 12 — GDSII Tape-Out Export")
open_eda.export_gds(OUTPUT_GDS, chip)
gds_size = os.path.getsize(OUTPUT_GDS)
print(f"  GDSII written: {OUTPUT_GDS}  ({gds_size:,} bytes)")

# ─── SUMMARY ──────────────────────────────────────────────────────────────────
hr("SUMMARY")
print(f"  Instances  : {chip.get_instance_count()}")
print(f"  Nets       : {lvs_report.net_count}")
print(f"  Setup WNS  : {sta.get_wns():.2f} ps")
print(f"  Hold  WNS  : {sta.get_hold_wns():.2f} ps")
print(f"  DRC viol.  : {total_drc}")
print(f"  LVS clean  : {lvs_report.clean()}")
print(f"  GDS size   : {gds_size:,} bytes")
sign_off = sta.get_wns() >= 0 and sta.get_hold_wns() >= 0 and total_drc == 0 and lvs_report.clean()
print(f"\n  {'TAPE-OUT READY' if sign_off else 'NOT YET READY FOR TAPE-OUT'}")
print('-' * 60)
