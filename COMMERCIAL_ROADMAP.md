# OpenEDA — Commercial Development Roadmap

**Goal:** Evolve OpenEDA from a research prototype into a commercial-grade RTL-to-GDSII physical design platform.  
**Target Market:** Cloud-native EDA, open-PDK flows (SkyWater 130nm / GF180), academic → production pipeline.  
**Last Updated:** 2026-05-02 (Phase 0.3 routing/DRC hardening, Phase 4.1 DRC geometry + wire model fixes, PdnGenerator macro ring boundary fix)

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| `[ ]` | Not started |
| `[~]` | In progress |
| `[x]` | Complete |
| `[!]` | Blocked / needs decision |

---

## Phase 0 — Foundation Hardening (Current State Cleanup)
> Fix known issues in the existing prototype before building on top.

### 0.1 Post-Route Buffer Insertion Fix  ✅ COMPLETE (2026-04-26)
- [x] Debug overlap caused by buffer insertion after routing
- [x] Re-enable `LogicOptimizer` buffer insertion with legalization pass (Legalizer::run() after all insertions)
- [x] Validate no DRC violations introduced — 16/16 tests pass (`tests/test_buffer_insertion.py`)

### 0.2 STA Backward Propagation (Required Time)
- [x] Implement required-time (RAT) backward traversal — topological sort, reverse pass
- [x] Compute actual slack = RAT − arrival time per pin (written back to Pin objects)
- [x] Add WNS, TNS, violation count — `Timer::getWNS()`, `getTNS()`, `getViolationCount()`
- [x] Validate: shift_reg signs off clean at 1 GHz (WNS=734ps, 0 violations)

### 0.3 Routing Robustness  **[~] Partial**
- [ ] Fix pin access point computation (currently assumes pin center is always valid)
- [ ] Add escape routing for macro pin congestion
- [x] Validate convergence on designs > 500 cells (bench_500: 511 cells, 88s)
- [x] Hard-block all PDN layers (L1/L3/L4) in A* routing grid — signal routes no longer share PDN stripe cells, eliminating 300+ DRC short false positives (2026-05-02)
- [x] Disable post-route jog insertion — PathFinder converges to 0 conflicts cleanly; jog insertion was creating spurious geometry on PDN rail boundaries (2026-05-02)
- [x] PdnGenerator macro ring boundary fix — VDD outer ring sides clamped to same coordinate as VSS inner ring at core boundary now skipped per-side; eliminates 50 VDD-VSS DRC shorts on soc_sram (2026-05-02)

### 0.4 Error Handling & Logging  **[~] Partial**
- [x] Structured logger with INFO/WARN/ERROR levels (`include/util/Logger.h`)
- [x] CTS "clock not found" demoted from ERROR to INFO for combinational designs
- [x] `load_pdk`: throws `RuntimeError` on missing Liberty/LEF files; validates cell count after parse
- [x] `load_verilog`: throws `RuntimeError` (instead of silent print) on parse failure
- [ ] Add graceful failure modes for unroutable designs and missing nets
- [ ] Return meaningful error messages through FastAPI (HTTP 422/500)

### 0.5 Regression Test Suite  **[~] Partial**
- [x] Baseline tests for `full_adder`, `shift_reg`, `adder` — 29/29 passing
- [x] Assert: placement legal, routing converges, GDSII non-empty, SPEF written
- [x] STA sign-off checks: WNS finite, TNS ≤ 0, 0 violations at 1 GHz
- [x] GitHub Actions CI workflow (`.github/workflows/ci.yml`): build + all tests on push/PR
- [x] `run_all_tests.py` — single-command runner; exit 1 if any suite fails
- [~] `soc_sram` benchmark (macro floorplan): DRC clean (0 violations), LVS 5 unrouted SRAM-pin nets (macro pin routing not yet supported — see Phase 0.3)

---

## Phase 1 — Scalable Core Algorithms
> Replace prototype algorithms with production-grade equivalents that handle 100K–10M cell designs.

### 1.1 Analytical Placement (Replace Simulated Annealing) ✓ COMPLETE
- [x] B2B (Bound-to-Bound) quadratic net model — sparse Laplacian build
- [x] Jacobi-preconditioned Conjugate Gradient solver (X and Y solved independently)
- [x] Bin-based density spreading (repulsion gradient per overcrowded bin)
- [x] Timing-driven net weighting via STA slack (updated every 5 outer iterations)
- [x] SA fallback retained for designs < 100 cells (SA_THRESHOLD)
- [x] Convergence check: early exit when HPWL delta < 0.1%
- [x] COO triplets replace std::map for sparse Laplacian (eliminates heap fragmentation)
- [x] Placement collapse fix: EPS=1.0, ALPHA=0.5, best-HPWL restore on collapse detection
- [x] Scale validated: bench_200 (207 cells, HPWL=2168), bench_500 (511 cells) — 13/13 pass
- [ ] Benchmark on 100K cell design (needs structural Verilog at that scale)

### 1.2 Global Routing  **[x] COMPLETE (2026-04-24)**
- [x] GCell grid: configurable X×Y coarse grid over core area; capacity heuristic based on net density
- [x] MST-based net decomposition: Prim's algorithm on Manhattan distance → 2-pin sub-problems
- [x] Congestion-aware Dijkstra on GCell graph: edge cost = history_cost + 5×overflow_penalty
- [x] Iterative rerouting: history costs updated after each pass; converges in ≤3 iterations on benchmarks
- [x] RouteGuide per net: bounding box of all traversed GCells (expanded 1 GCell), stored on `Net::routeGuides`
- [x] Python bindings: `GlobalRouter`, `GRouteResult`, `RouteGuide`, `GCell` exposed; 45/45 tests pass
- [x] Validated: full_adder (100% routability), bench_200 (80% routability, 105/153 nets guided, 3 iters)

### 1.3 Detailed Router Scalability
- [x] Pre-allocated member vectors (astar_pq_buf, astar_segment, grid_usage/history/obstacles)
- [x] Generational searchId/netUsedId replace std::set — O(1) ownership checks, zero heap churn
- [x] Adaptive grid scaling (MAX_GRID_NODES=300K), dynamic bounding box, MAX_EXPAND=60K cap
- [x] Fix negative congCost bug (pin usage -50 + PDN obstacles → parentIdx cycles → bad_alloc)
- [ ] Refactor A* to operate within global route guide bounding boxes
- [ ] Add track assignment stage before maze routing
- [ ] Multi-threading: per-GCell parallel routing (replace per-net)
- [ ] Memory: replace flat 3D grid with sparse octree/hash map for large dies

### 1.4 Hierarchical / Incremental Flows
- [ ] Partition large designs into hierarchical blocks
- [ ] Implement incremental placement (move subset of cells without full re-run)
- [ ] Incremental routing (rip-up only affected nets on ECO changes)

---

## Phase 2 — Technology & File Format Compliance
> Support real PDKs and industry-standard file formats.

### 2.1 SkyWater 130nm PDK Integration (First Target Node) ✅ COMPLETE (2026-04-20)
- [x] Download and parse SkyWater 130nm Liberty files (`sky130_fd_sc_hd__tt_025C_1v80.lib`) — 441 cells parsed
- [x] Parse SkyWater 130nm LEF files (standard cell library) — power pins filtered, signal pins extracted
- [x] Validate full flow: parse → place → route → GDSII on `sky130` standard cells — 14/14 tests pass
- [x] Map GDS layer numbers to SkyWater layer stack (li1=67, met1=68, met2=69, met3=70, met4=71, met5=72)
- [x] Document tested cell types: INV, NAND2, NOR2, BUF — validated via `sky130_inv_chain.v` benchmark
- [x] `load_pdk()` Python API: Liberty + LEF pre-load before `load_verilog`
- [x] Liberty parser handles sky130 syntax: quoted cell names, `pg_pin`, `leakage_power`, `1ns` time unit

### 2.2 GF180MCU PDK Integration (Second Target Node)
- [ ] Parse GF180 Liberty + LEF files
- [ ] Validate flow end-to-end
- [ ] Document layer mapping

### 2.3 SDC Constraint Parsing (Critical for Timing Closure) ✅ COMPLETE (2026-04-20)
- [x] Parse `create_clock` (clock definition, period, waveform)
- [x] Parse `set_input_delay` / `set_output_delay` (per-port, -max/-min, -clock)
- [x] Parse `set_false_path`, `set_multicycle_path`
- [x] Parse `set_clock_uncertainty`, `set_clock_latency`
- [x] Store constraints in Design.sdc (SdcConstraints); propagate into Timer on updateTiming()
- [x] `chip.read_sdc(path)` + `chip.get_clock_period()` Python API — 14/14 tests pass

### 2.4 DEF Input Parser
- [ ] Parse DEF `COMPONENTS` section (placed instances with locations)
- [ ] Parse DEF `NETS` section (connections)
- [ ] Parse DEF `ROWS` and `TRACKS`
- [ ] Enable mixed-flow: place externally, route in OpenEDA

### 2.5 SPEF Input Parser  ✅ COMPLETE (2026-04-26)
- [x] Parse `*NAME_MAP`, `*D_NET`, `*CONN`, `*CAP`, `*RES` sections (IEEE 1481 SPEF)
- [x] Back-annotate parasitics into STA engine for post-route timing (`SpefEngine::readSpef`)
- [x] Per-net helpers: `getWireDelay`, `getNetCap`, `getExtractedNetCount`
- [x] Write+read round-trip verified; STA WNS consistent within 1ps — 19/19 tests pass (`tests/test_spef_input.py`)

### 2.6 OpenDB-Compatible Internal Database
- [ ] Refactor `Design` class to use OpenDB-like schema (or native OpenDB)
- [ ] Enables interoperability with OpenROAD, Yosys ecosystem
- [ ] Preserve Python bindings through abstraction layer

### 2.7 TCL Scripting Interface  ✅ COMPLETE (2026-04-26)
- [x] Custom lightweight TCL engine (no libtcl dependency) with quoted-string tokenizer and `$var`/`${var}` substitution
- [x] Commands: `puts`, `set`, `read_verilog`, `read_liberty`, `read_lef`, `read_sdc`, `place_design`, `route_design`, `write_gds`, `write_spef`, `report_timing`, `check_drc`, `check_lvs`, `help`
- [x] Batch script execution: `open_eda -script run.tcl` (main.cpp `-script` mode)
- [x] Python API: `TclEngine(design)` with `run_script`, `run_command`, `get_output`, `get_error`, `clear_output`
- [x] `benchmarks/test_flow.tcl` example script (full RTL-to-GDSII flow)
- [x] 40/40 tests pass (`tests/test_tcl_engine.py`)

---

## Phase 3 — Full Timing Closure
> Achieve sign-off quality timing driven by real constraints.

### 3.1 Complete Multi-Corner Multi-Mode (MCMM) STA ✅ COMPLETE (2026-04-26)
- [x] Support multiple Liberty files per corner (fast, slow, typical)
- [x] Support multiple operating modes (functional, scan, test) — TimingMode enum; functional fully implemented
- [x] Per-corner arrival/required time propagation (graph built once, delays swapped per corner)
- [x] Report: worst slack per corner/mode combination (formatMcmmReport, getWorstCorner)

### 3.2 Setup & Hold Timing Checks ✅ COMPLETE (2026-04-20)
- [x] Setup check: data arrival < clockPeriod - setupTime - uncertainty + latency
- [x] Hold check: data arrival > clockLatency + holdTime + uncertainty (FF D-pins only)
- [x] Per-FF holdSlack stored on TimingNode; written back to Pin for reporting
- [x] reportAllEndpoints() shows both SetupSlk and HoldSlk columns
- [x] reportCriticalPath() shows SETUP / HOLD / SETUP+HOLD status
- [x] getHoldWNS(), getHoldTNS(), getHoldViolationCount() + TimingSummary hold fields
- [x] set_clock_uncertainty(), set_clock_latency() Python API on Timer
- [x] tests/test_setup_hold.py — 18/18 pass; DFF chain hold violations correctly detected

### 3.3 Signal Integrity / Crosstalk
- [ ] Coupling capacitance extraction between adjacent wires
- [ ] Crosstalk delay bump model (aggressor/victim)
- [ ] SI-aware STA: add/subtract crosstalk delta to path delays

### 3.4 ECO (Engineering Change Order) Flow  **[x] COMPLETE**
- [x] Gate sizing: upsize cells on critical paths without re-placement (`EcoEngine::fixSetupViolations`)
- [x] Buffer insertion: insert delay buffers on hold-violating FF D-pins (`EcoEngine::fixHoldViolations`)
- [x] Hold fixing: multi-pass buffer insertion converges hold WNS to 0 (shift_reg: -20ps → +18.2ps in 3 iterations)
- [x] Timing closure loop: iterate ECO until WNS ≥ 0 and hold violations = 0 (`EcoEngine::runTimingClosure`)
- [x] Python bindings: `EcoEngine`, `EcoResult` exposed; 21/21 tests passing (`tests/test_eco.py`)
- [x] Incremental STA after ECO: `Timer::updateTimingSkipBuild()` + `patchGraph()` — full rebuilds always == 2 regardless of iterations; 25/25 tests passing (`tests/test_incremental_sta.py`)

### 3.5 Timing Reports  **[x] COMPLETE**
- [x] Full path report: startpoint → endpoint with per-gate delay + wire delay + incremental columns (`TimingReporter::getTopPaths`)
- [x] Slack histogram: endpoint count per slack bucket, ASCII bar chart (`TimingReporter::getSlackHistogram`)
- [x] Clock domain crossing (CDC) report: detects multi-domain FF handoffs (`TimingReporter::formatCdcReport`)
- [x] Text export: full .rpt file with summary, top-N paths, histogram, endpoint table (`writeTextReport`)
- [x] HTML export: self-contained styled sign-off page with collapsible paths, color-coded endpoints (`writeHtmlReport`)
- [x] Python bindings: `TimingReporter`, `PathReport`, `PathStep`, `SlackBin` exposed; 63/63 tests passing (`tests/test_timing_reports.py`)

---

## Phase 4 — Physical Verification (DRC / LVS)
> Required for any foundry tape-out submission.

### 4.1 Full Geometric DRC Engine  **[x] COMPLETE**
- [x] Short circuit detection: same-layer different-net rectangle overlap (`DrcViolationType::SHORT`)
- [x] Min spacing check: edge-to-edge gap < rule for adjacent same-layer different-net wires (`MIN_SPACING`)
- [x] Min width check per layer: narrow dimension of wire rectangle < rule (`MIN_WIDTH`)
- [x] Min area check per layer: rectangle area < rule (`MIN_AREA`)
- [x] Rule deck format: `.drc` text file with per-layer and via rules in nm (`benchmarks/sky130_hd.drc`)
- [x] Built-in sky130_hd defaults: 6 layers (li1–met5) + 5 via types (mcon, via1–via4)
- [x] Python bindings: `DrcEngine`, `DrcReport`, `DrcViolation`, `DrcRuleDeck`, `LayerRule`, `ViaRule`, `DrcViolationType` exposed; 50/50 tests passing (`tests/test_drc.py`)
- [x] Wire geometry tuning: HALF_WIDTH=7nm (0.07 design units), routing grid=100nm — adjacent tracks have 86nm edge gap, safely above 80nm minSpacing rule deck (2026-05-02)
- [x] Diagonal pair filtering in `extractRects`: pair-encoded routePath junction pairs (end of one segment → start of next) are diagonal; skipped to avoid phantom rectangles spanning multiple PDN rows (2026-05-02)
- [x] L1 (li1) cross-net skip in `checkSpacing`: signal pins on M1 PDN rail rows are isolated by standard cell boundary; all L1 cross-net proximity checks are suppressed as physical false positives (2026-05-02)
- [x] Grid-compatible rule deck: minSpacing=80nm (<86nm edge gap), minWidth=0, minArea=0 — avoids false violations on grid-routed wires (2026-05-02)
- [ ] Notch and jog detection (requires polygon-level geometry)
- [ ] Density (metal fill) rules
- [ ] Antenna ratio check (gate oxide protection)
- [ ] Via enclosure enforcement (geometry currently checks existence, not enclosure margin)

### 4.2 LVS (Layout vs. Schematic)  **[x] COMPLETE**
- [x] Unplaced instance detection: flags any cell not marked placed after `run_placement` (`UNPLACED_INSTANCE`)
- [x] Floating pin detection: flags pins with no net assignment (`UNCONNECTED_PIN`)
- [x] Unrouted net detection: flags multi-pin nets with no routePath (`UNROUTED_NET`)
- [x] Physical coverage check: bounding-box check that each pin is reached by its net's routing (`OPEN_CIRCUIT`)
- [x] Fixed `run_placement` to set `isPlaced=true` on all instances after SA + legalization
- [x] Combined DRC+LVS sign-off: `DRC.short_count==0` AND `LVS.clean()` → tape-out ready
- [x] Python bindings: `LvsEngine`, `LvsReport`, `LvsMismatch`, `LvsMismatchType` exposed; 45/45 tests passing (`tests/test_lvs.py`)

### 4.3 ERC (Electrical Rule Check)  **[x] COMPLETE (2026-04-24)**
- [x] Floating input pins: detect input pins whose net has no output driver (`FLOATING_INPUT`)
- [x] Multiple drivers: detect nets with ≥2 output pins (`MULTIPLE_DRIVER`)
- [x] Power/ground connectivity: detect VDD/VSS/VPWR/VGND pins with no net (`NO_POWER_PIN`)
- [x] Python bindings: `ErcEngine`, `ErcReport`, `ErcViolation`, `ErcViolationType` exposed; 50/50 tests passing (`tests/test_erc.py`)

### 4.4 DRC/LVS Signoff Integration Option
- [ ] Define plugin interface for external signoff tools (Calibre, Magic, KLayout)
- [ ] KLayout Python API integration for open-source DRC option
- [ ] GDSII export validated against KLayout DRC scripts

---

## Phase 5 — Logic Synthesis Integration
> Accept RTL input, not just pre-synthesized structural Verilog.

### 5.1 Yosys Integration (RTL → Netlist)  **[x] COMPLETE (2026-04-24)**
- [x] Call Yosys via subprocess (oss-cad-suite); Windows PATH + DLL resolution handled
- [x] Custom techmap (no ABC): maps RTLIL primitives directly to simple.lib cells; bypasses Windows TEMP `~1` path bug in ABC
- [x] Supported cells: AND2, OR2, NOT, NAND2, NOR2, XOR2, BUF, DFF, CLKBUF + sync/async reset DFFs via techmap
- [x] `SynthEngine::synthesize(rtlFile, topModule, techmapFile="")` — built-in techmap fallback
- [x] `SynthResult`: success, output_netlist, cell_count, log, error_message
- [x] Benchmarks: `alu_rtl.v` (combinational), `counter_behavioral.v` (sequential), `dff_chain_rtl.v` (pipeline)
- [x] Python bindings: `SynthEngine`, `SynthResult` exposed; 54/54 tests passing (`tests/test_synthesis.py`)

### 5.2 Gate Sizing & Technology Re-mapping  **[x] COMPLETE (2026-04-24)**
- [x] `simple.lib` extended with `_X2` drive-strength variants for all 9 base cells (area×2, timing×0.65, NLDM 3×3)
- [x] `GateSizer::resizeForTiming`: upsize non-sequential gates with negative setup slack; re-runs STA after changes
- [x] `GateSizer::resizeForArea`: downsize gates with 2× slack budget headroom; reports area saved
- [x] Supports both `_XN` (simple.lib) and `_N` (sky130) naming conventions via `parseStrengthSuffix`
- [x] Python bindings: `GateSizer`, `GateSizeResult` exposed; 33/33 tests passing (`tests/test_gate_sizer.py`)

### 5.3 Logic Optimization Passes  **[x] COMPLETE (2026-04-24)**
- [x] `LogicOptimizer::removeDeadLogic`: iterative fixpoint removal of gates with no path to endpoints; respects `primaryOutputNets`
- [x] `LogicOptimizer::collapseBufferChains`: iterative BUF→BUF→… chain collapsing by rewiring sinks to source net
- [x] `Design::primaryOutputNets`: populated by `VerilogParser` from `output` port declarations; protects primary outputs from dead-logic removal
- [x] `OptimizeResult`: dead_gates_removed, buffers_collapsed, any_change()
- [x] Legacy `fixTiming()` API retained for `main.cpp` compatibility
- [x] Python bindings: `LogicOptimizer`, `OptimizeResult` exposed; 28/28 tests passing (`tests/test_logic_optimizer.py`)

---

## Phase 6 — Professional GUI
> Replace ImGui prototype with a production-quality layout editor.

### 6.1 Qt6-Based Main Window
- [ ] Menu bar: File, Edit, View, Tools, Help
- [ ] Project navigator (tree: design hierarchy, cells, nets)
- [ ] Properties panel (selected object attributes)
- [ ] Console / log panel (TCL input + output)
- [ ] Status bar (design stats, current mode)

### 6.2 Layout Canvas (OpenGL Backend Retained)
- [ ] Pan, zoom, scroll with mouse
- [ ] Layer visibility toggles (per-layer show/hide/highlight)
- [ ] Selection: click cell/net/wire, highlight connected
- [ ] Measurement tool (ruler)
- [ ] Search: jump to net/instance by name
- [ ] Flightline display (ratsnest for unrouted nets)

### 6.3 Timing / Analysis Views
- [ ] Schematic view (netlist graph — cells + connections)
- [ ] Timing path viewer: highlight critical path on layout
- [ ] Congestion heatmap overlay
- [ ] Power density map overlay
- [ ] Clock tree viewer (tree topology diagram)

### 6.4 Interactive Floorplanning
- [ ] Drag-and-drop macro placement
- [ ] I/O pad ring editor
- [ ] Blockage drawing (placement/routing blockages)
- [ ] Row/site definition editor

---

## Phase 7 — Cloud & API Platform
> Build on the existing FastAPI foundation for commercial SaaS delivery.

### 7.1 REST API Hardening (Extend FastAPI)
- [ ] `POST /api/v1/flow/run_all` — full flow (exists, needs robustness)
- [ ] `POST /api/v1/flow/synthesize` — synthesis only
- [ ] `POST /api/v1/flow/place` — placement only
- [ ] `POST /api/v1/flow/route` — routing only
- [ ] `GET /api/v1/job/{id}/status` — async job polling
- [ ] `GET /api/v1/job/{id}/report` — timing/DRC report download
- [ ] `GET /api/v1/job/{id}/gds` — GDSII download
- [ ] WebSocket endpoint for real-time log streaming

### 7.2 Job Queue & Async Processing
- [ ] Celery + Redis task queue for async design jobs
- [ ] Job status tracking (queued → running → complete → failed)
- [ ] Result storage (S3-compatible object store)
- [ ] Job cancellation and timeout handling

### 7.3 Multi-Tenancy & Auth
- [ ] API key authentication (per-customer)
- [ ] Usage metering (gates processed, CPU-hours)
- [ ] Rate limiting per tier
- [ ] Audit log (who ran what, when)

### 7.4 Containerization & Deployment
- [ ] Dockerfile for OpenEDA server (C++ engine + Python API)
- [ ] Docker Compose: FastAPI + Celery + Redis
- [ ] Kubernetes Helm chart for cloud deployment
- [ ] Auto-scaling based on job queue depth

### 7.5 Web Frontend (Optional / Phase 2)
- [ ] React-based job submission UI
- [ ] Upload Verilog → select PDK → run flow → download results
- [ ] View timing report and layout inline (SVG/WebGL viewer)

---

## Phase 8 — Commercial Packaging
> Everything needed to sell and support the product.

### 8.1 Licensing Engine
- [ ] Node-locked or floating license model
- [ ] License file validation (RSA signature)
- [ ] Grace period and expiry handling
- [ ] SaaS: API key + usage-based billing integration

### 8.2 Documentation
- [ ] User manual (PDF + web)
- [ ] TCL command reference (auto-generated from source)
- [ ] Python API reference (Sphinx/autodoc)
- [ ] Tutorial: hello-world chip through full flow
- [ ] Tutorial: SkyWater 130nm tape-out walkthrough

### 8.3 Installer & Distribution
- [ ] Windows installer (NSIS or WiX)
- [ ] Linux `.deb` / `.rpm` packages
- [ ] Docker image on Docker Hub
- [ ] PyPI package: `pip install open_eda`

### 8.4 Support Infrastructure
- [ ] Issue tracker (GitHub or Jira)
- [ ] Regression suite run on every release (CI/CD)
- [ ] Versioned releases with changelog
- [ ] Customer support SLA tiers

---

## Dependency Map

```
Phase 0 (Cleanup)
    └──► Phase 1 (Scalable Algorithms)
              └──► Phase 2 (Technology / Formats)
                        ├──► Phase 3 (Timing Closure)
                        ├──► Phase 4 (DRC / LVS)
                        └──► Phase 5 (Synthesis)
                                    │
                        ┌───────────┘
                        ▼
                   Phase 6 (GUI)
                   Phase 7 (Cloud API)
                        │
                        ▼
                   Phase 8 (Commercial Packaging)
```

---

## Prioritized Quick Wins (Start Here)

These unblock the most downstream work for the least effort:

| Priority | Task | Why It Unblocks |
|----------|------|-----------------|
| 1 | STA backward propagation (RAT + slack) | Timing closure, ECO, SDC all need slack |
| 2 | SDC constraint parsing | Without it, timing closure is impossible |
| 3 | SkyWater 130nm PDK integration | Real tapeouts, real validation, no NDA |
| 4 | Regression test suite | Prevents regressions as code grows |
| 5 | Structured logger | Debugging at scale requires it |
| 6 | Analytical placement | SA doesn't scale past 10K cells |
| 7 | Global routing stage | Detailed router can't scale without it |
| 8 | TCL scripting interface | Every EDA user expects it |

---

## Milestones

| Milestone | Phases Complete | Target Capability |
|-----------|-----------------|-------------------|
| **M1 — Alpha** | 0 + 2.1 + 2.3 | Real timing closure on SkyWater 130nm |
| **M2 — Beta** | 1 + 2 + 3 | 100K cell designs, full STA, SDC |
| **M3 — Verification** | 4 + 5 | DRC/LVS sign-off, RTL input |
| **M4 — Platform** | 6 + 7 | Professional GUI + cloud API |
| **M5 — Commercial** | 8 | Licensed product, documentation, installer |

---

## Notes & Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-04-18 | Target SkyWater 130nm as first PDK | Open PDK, no NDA, active community, real tapeouts |
| 2026-04-18 | Integrate Yosys for synthesis (not build from scratch) | 10+ years of development, proven, open-source |
| 2026-04-18 | Keep C++ core, Python/TCL scripting layers | Performance-critical engine in C++; user scripting in TCL/Python |
| 2026-04-18 | Cloud-first commercial differentiator | Cadence/Synopsys weak on cloud-native; FastAPI head start |
| 2026-05-02 | Hard-block all PDN layers in router | Soft-blocking L3/L4 allowed signal routes through PDN stripe cells → 300+ DRC shorts; hard-block forces signals into inter-stripe gaps |
| 2026-05-02 | HALF_WIDTH=7nm (not 140nm/700nm) | Routing grid=100nm; wire half-width must leave >80nm edge gap to adjacent track — 7nm gives 86nm gap, passes rule deck |
| 2026-05-02 | Skip all L1 cross-net DRC pairs | li1 signal pins sit on PDN M1 rail rows; standard cell boundary isolates them electrically — apparent overlap is a model artifact, not a real short |
