# OpenEDA — Commercial Development Roadmap

**Goal:** Evolve OpenEDA from a research prototype into a commercial-grade RTL-to-GDSII physical design platform.  
**Target Market:** Cloud-native EDA, open-PDK flows (SkyWater 130nm / GF180), academic → production pipeline.  
**Last Updated:** 2026-04-20

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

### 0.1 Post-Route Buffer Insertion Fix
- [ ] Debug overlap caused by buffer insertion after routing
- [ ] Re-enable `LogicOptimizer` buffer insertion with legalization pass
- [ ] Validate no DRC violations introduced

### 0.2 STA Backward Propagation (Required Time)
- [x] Implement required-time (RAT) backward traversal — topological sort, reverse pass
- [x] Compute actual slack = RAT − arrival time per pin (written back to Pin objects)
- [x] Add WNS, TNS, violation count — `Timer::getWNS()`, `getTNS()`, `getViolationCount()`
- [x] Validate: shift_reg signs off clean at 1 GHz (WNS=734ps, 0 violations)

### 0.3 Routing Robustness
- [ ] Fix pin access point computation (currently assumes pin center is always valid)
- [ ] Add escape routing for macro pin congestion
- [x] Validate convergence on designs > 500 cells (bench_500: 511 cells, 88s)

### 0.4 Error Handling & Logging
- [x] Structured logger with INFO/WARN/ERROR levels (`include/util/Logger.h`)
- [x] CTS "clock not found" demoted from ERROR to INFO for combinational designs
- [ ] Add graceful failure modes (corrupt LEF/Liberty, missing nets, unroutable designs)
- [ ] Return meaningful error messages through Python bindings and FastAPI

### 0.5 Regression Test Suite
- [x] Baseline tests for `full_adder`, `shift_reg`, `adder` — 29/29 passing
- [x] Assert: placement legal, routing converges, GDSII non-empty, SPEF written
- [x] STA sign-off checks: WNS finite, TNS ≤ 0, 0 violations at 1 GHz
- [ ] Add `soc_sram` benchmark (macro floorplan test)
- [ ] Integrate into CI (GitHub Actions)

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

### 1.2 Global Routing (New Stage Between Placement and Detail Route)
- [ ] Build routing grid (GCell-based coarse graph)
- [ ] Implement Steiner tree approximation for net topology
- [ ] Congestion estimation per GCell
- [ ] Overflow-driven rerouting (layer assignment)
- [ ] Output: per-net layer-assigned global route guides
- [ ] Feed guides into A* detailed router to constrain search space

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

### 2.3 SDC Constraint Parsing (Critical for Timing Closure)
- [ ] Parse `create_clock` (clock definition, period, waveform)
- [ ] Parse `set_input_delay` / `set_output_delay`
- [ ] Parse `set_false_path`, `set_multicycle_path`
- [ ] Parse `set_clock_uncertainty`, `set_clock_latency`
- [ ] Store constraints in Design object; propagate into STA engine

### 2.4 DEF Input Parser
- [ ] Parse DEF `COMPONENTS` section (placed instances with locations)
- [ ] Parse DEF `NETS` section (connections)
- [ ] Parse DEF `ROWS` and `TRACKS`
- [ ] Enable mixed-flow: place externally, route in OpenEDA

### 2.5 SPEF Input Parser
- [ ] Parse `*D_NET` sections (distributed RC)
- [ ] Back-annotate parasitics into STA engine for post-route timing

### 2.6 OpenDB-Compatible Internal Database
- [ ] Refactor `Design` class to use OpenDB-like schema (or native OpenDB)
- [ ] Enables interoperability with OpenROAD, Yosys ecosystem
- [ ] Preserve Python bindings through abstraction layer

### 2.7 TCL Scripting Interface
- [ ] Embed TCL interpreter (libtcl)
- [ ] Expose all engine commands as TCL procedures: `read_verilog`, `read_lef`, `place_design`, `route_design`, `write_gds`, etc.
- [ ] Batch script execution: `open_eda -script run.tcl`
- [ ] Industry expectation: all commercial EDA tools are TCL-driven

---

## Phase 3 — Full Timing Closure
> Achieve sign-off quality timing driven by real constraints.

### 3.1 Complete Multi-Corner Multi-Mode (MCMM) STA
- [ ] Support multiple Liberty files per corner (fast, slow, typical)
- [ ] Support multiple operating modes (functional, scan, test)
- [ ] Per-corner arrival/required time propagation
- [ ] Report: worst slack per corner/mode combination

### 3.2 Setup & Hold Timing Checks
- [ ] Setup check: data arrival < clock arrival − setup_time
- [ ] Hold check: data arrival > clock arrival + hold_time
- [ ] Per-flip-flop slack reporting
- [ ] Identify setup-critical vs. hold-critical paths separately

### 3.3 Signal Integrity / Crosstalk
- [ ] Coupling capacitance extraction between adjacent wires
- [ ] Crosstalk delay bump model (aggressor/victim)
- [ ] SI-aware STA: add/subtract crosstalk delta to path delays

### 3.4 ECO (Engineering Change Order) Flow
- [ ] Gate sizing: upsize/downsize cells on critical paths without re-placement
- [ ] Buffer insertion: fix long-wire delays on critical paths (fix existing bug first)
- [ ] Hold fixing: insert delay buffers on hold-violating paths
- [ ] Incremental STA update after ECO (no full re-run)
- [ ] Timing closure loop: iterate ECO until WNS ≥ 0 and TNS = 0

### 3.5 Timing Reports
- [ ] Full path report: startpoint → endpoint with per-gate delay breakdown
- [ ] Slack histogram
- [ ] Clock domain crossing (CDC) report
- [ ] Export to HTML and text formats

---

## Phase 4 — Physical Verification (DRC / LVS)
> Required for any foundry tape-out submission.

### 4.1 Full Geometric DRC Engine
- [ ] Min width check per layer
- [ ] Min spacing check per layer (same-net and different-net)
- [ ] Notch and jog detection
- [ ] Minimum area check per layer
- [ ] Via stacking and enclosure rules
- [ ] Density (metal fill) rules
- [ ] Antenna ratio check (gate oxide protection)
- [ ] Rule deck input format: define rules in config file (not hardcoded)

### 4.2 LVS (Layout vs. Schematic)
- [ ] Extract netlist from GDSII layout
- [ ] Compare extracted netlist vs. input Verilog netlist
- [ ] Report: missing connections, extra connections, device mismatches
- [ ] Integrate with DRC pass/fail signoff

### 4.3 ERC (Electrical Rule Check)
- [ ] Floating input pins
- [ ] Multiple drivers on same net
- [ ] Power/ground connectivity check

### 4.4 DRC/LVS Signoff Integration Option
- [ ] Define plugin interface for external signoff tools (Calibre, Magic, KLayout)
- [ ] KLayout Python API integration for open-source DRC option
- [ ] GDSII export validated against KLayout DRC scripts

---

## Phase 5 — Logic Synthesis Integration
> Accept RTL input, not just pre-synthesized structural Verilog.

### 5.1 Yosys Integration (RTL → Netlist)
- [ ] Embed or call Yosys for RTL synthesis
- [ ] Support: SystemVerilog, Verilog-2005 input
- [ ] Technology mapping using Liberty cell library
- [ ] Output: structural Verilog netlist → feed into OpenEDA placement
- [ ] Expose via TCL: `synth_design -top <module> -liberty <lib>`

### 5.2 Gate Sizing & Technology Re-mapping
- [ ] Swap gates to different drive strengths from same cell family
- [ ] Timing-driven resizing: upsize on critical path, downsize off-critical
- [ ] Area-driven resizing: minimize total area given timing constraints

### 5.3 Logic Optimization Passes
- [ ] Constant propagation and dead logic removal
- [ ] Re-synthesis for area reduction on non-critical paths

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
