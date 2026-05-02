# OpenEDA — Byteflow Silicon Compiler

**A from-scratch C++ RTL-to-GDSII Physical Design Engine**

OpenEDA is a fully automated VLSI physical design flow built from the ground up in C++17. It accepts RTL or structural Verilog and executes the complete chip implementation pipeline — synthesis, placement, clock tree synthesis, power delivery, global and detailed routing, parasitic extraction, static timing analysis, DRC, LVS, and GDSII tape-out — producing foundry-ready binary layout files.

---

## Pipeline Overview

```
RTL / Structural Verilog (.v)
        │
   ┌────▼──────────────────┐
   │  Synthesis (Yosys)    │  RTL → gate-level netlist · techmap · logic opt
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Library Parsing      │  Liberty timing arcs · LEF pin geometry · macros
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Floorplanning        │  Macro placement · core area definition
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Global Placement     │  Analytical (quadratic) + SA · timing-driven
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Clock Tree Synthesis │  MMM tree · CLKBUF insertion · skew balancing
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Power Delivery (PDN) │  M1 rails · M3/M4 stripe mesh · via arrays
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Global Routing       │  GCell grid · MST decomposition · Dijkstra
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Detailed Routing     │  A* maze · PathFinder negotiation · 7 layers
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  RC Extraction (SPEF) │  Elmore RC model · per-net R/C · SPEF write/read
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Static Timing (STA)  │  NLDM · MCMM · setup/hold · ECO closure
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  DRC / LVS / ERC      │  Geometric rule check · netlist verify · ERC
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  GDSII Tape-Out       │  Big-endian binary · hierarchical SREF cells
   └────┬──────────────────┘
        │
        ▼
   output.gds  ←  Foundry-ready layout
```

---

## Quick Start

```bash
# Build
mkdir build && cd build
cmake .. -DBUILD_PYTHON_MODULE=ON
cmake --build . --config Release

# Run full RTL-to-GDSII flow
python run.py
```

**Expected output:**
```
  DRC viol.  : 0
  LVS clean  : True
  Setup WNS  : 894.38 ps
  Hold  WNS  : 0.00 ps

  TAPE-OUT READY
```

---

## Engine Modules

### 1. Synthesis (Yosys Integration)
Calls Yosys via subprocess to synthesize RTL into a gate-level netlist using the built-in cell library. Supports combinational and sequential designs including DFFs with sync/async reset.

```python
synth = open_eda.SynthEngine()
result = synth.synthesize("designs/counter.v", "counter")
print(f"Cells: {result.cell_count}")
```

### 2. Library Parsing (LEF / Liberty)
Parses `.lib` for NLDM timing arcs (setup, hold, propagation) and `.lef` for pin geometry, macro bounding boxes, and physical dimensions. Supports SkyWater 130nm (`sky130_fd_sc_hd`) out of the box.

### 3. Floorplanning
Places hard macros (SRAM, analog blocks) at fixed locations. Defines core area, standard cell rows, and PDN clearances.

### 4. Global Placement
- **Analytical placer**: Quadratic net model with Jacobi-preconditioned Conjugate Gradient solver and bin-based density spreading
- **SA fallback** for small designs (< 100 cells)
- **Timing-driven**: net weights updated from STA slack every 5 iterations
- Validated: bench_200 (207 cells), bench_500 (511 cells)

### 5. Clock Tree Synthesis (CTS)
Recursive Method of Means and Medians tree builder with automatic `CLKBUF` insertion, branch-point legalization, and sub-net weaving for balanced skew across all flip-flop sinks.

### 6. Power Delivery Network (PDN)
- **M1 horizontal rails** every 10 µm — VDD/VSS alternating standard cell rows
- **M3 vertical stripes** every 40 µm with VDD/VSS pair offset
- **M4 horizontal stripes** every 40 µm with M4→M3 via arrays
- **Macro power rings** — VSS inner / VDD outer rings with per-side boundary guards

### 7. Global Routing
GCell grid over core area with MST-based 2-pin net decomposition and congestion-aware Dijkstra. Generates route guides bounding boxes per net for the detailed router.

### 8. Detailed Router (A* Maze)
Multi-layer 3D routing engine:
- **PathFinder negotiation** — history-based penalty ramp for rip-up and reroute
- **Hard PDN blockage** — all PDN layer cells excluded from signal routing grid
- **7-layer grid** (li1, met1–met5 + via layers)
- **OpenMP parallelized** per-net A* search
- **Convergence**: 0 routing conflicts on all validated benchmarks

### 9. RC Parasitic Extraction (SPEF)
Elmore RC model extracted from routed geometry. Outputs IEEE 1481 SPEF files with per-net R, C, and Elmore delay. Supports SPEF read-back for post-route STA back-annotation.

### 10. Static Timing Analysis (STA)
- **NLDM timing arcs** from Liberty (setup time, hold time, propagation delay)
- **Elmore wire delay** from extracted parasitics
- **Setup and hold checks** per flip-flop endpoint
- **Multi-Corner Multi-Mode (MCMM)** — worst slack per corner/mode
- **ECO closure** — gate sizing and buffer insertion to fix violations

### 11. Physical Verification

**DRC (Design Rule Check)**
- Short circuit detection (same-layer, different-net overlap)
- Min spacing / min width / min area per layer
- Built-in SkyWater 130nm rule deck (li1–met5, mcon/via1–via4)
- Wire geometry: HALF_WIDTH=7nm on 100nm routing grid → 86nm edge gap

**LVS (Layout vs. Schematic)**
- Unplaced instance detection
- Floating pin detection
- Unrouted net detection
- Physical coverage (bounding-box open circuit check)

**ERC (Electrical Rule Check)**
- Floating input pins (no driver)
- Multiple drivers on same net
- Missing VDD/VSS connectivity

### 12. GDSII Tape-Out Export
- **Big-endian binary** with x86 byte-swap
- **IBM Real8** floating-point for UNITS records
- **Hierarchical structures** — each cell type gets its own STRUCTURE
- **SREF instantiation** — no polygon duplication
- **Layer mapping**: li1→1, met1→2, met2→3, met3→4, met4→5, met5→6
- **Wire expansion**: centerline segments → closed-loop copper rectangles

### 13. TCL Scripting Interface
Full TCL engine with variable substitution and all flow commands:
```tcl
read_verilog benchmarks/full_adder.v
read_liberty benchmarks/simple.lib
place_design
route_design
check_drc
report_timing
write_gds output.gds
```

---

## Python API

```python
import open_eda

# Load design
chip = open_eda.Design()
chip.load_verilog("benchmarks/full_adder.v")

# Run flow
open_eda.run_placement(chip)

cts = open_eda.CtsEngine()
cts.run_cts(chip, "clk")

pdn = open_eda.PdnGenerator(chip)
pdn.run()

router = open_eda.RouteEngine()
router.route(chip)

# Extract parasitics
spef = open_eda.SpefEngine()
spef.extract(chip)
spef.write_spef("output.spef", chip)

# Static timing
sta = open_eda.Timer(chip, spef)
sta.build_graph()
sta.set_clock_period(1000.0)   # 1 GHz
sta.update_timing()
print(f"Setup WNS: {sta.get_wns():.2f} ps")
print(f"Hold  WNS: {sta.get_hold_wns():.2f} ps")

# DRC + LVS
drc = open_eda.DrcEngine()
drc_report = drc.run_drc(chip)
print(f"DRC violations: {drc_report.total_count()}")

lvs = open_eda.LvsEngine()
lvs_report = lvs.run_lvs(chip)
print(f"LVS clean: {lvs_report.clean()}")

# Export
open_eda.export_gds("output.gds", chip)
```

---

## Validated Benchmarks

| Design | Cells | DRC | LVS | Setup WNS | Status |
|--------|-------|-----|-----|-----------|--------|
| `full_adder` | 5 | 0 violations | Clean | 894 ps | **TAPE-OUT READY** |
| `shift_reg` | 4 DFFs | 0 violations | Clean | — | **TAPE-OUT READY** |
| `soc_sram` | 7 + SRAM macro | 0 violations | 5 unrouted SRAM pins | 963 ps | DRC clean |
| `bench_200` | 207 | — | — | — | Placement validated |
| `bench_500` | 511 | — | — | — | Routing convergence |

---

## Build

### Prerequisites
- C++17 compiler (GCC 11+ / Clang 14+ / MSVC 2022)
- CMake 3.16+
- Python 3.10+ with pybind11
- OpenMP (optional, for parallel routing)

### Build Python Module
```bash
mkdir build && cd build
cmake .. -DBUILD_PYTHON_MODULE=ON
cmake --build . --config Release
```

### Run Full Flow
```bash
python run.py
```

### Run Specific Design
Edit `VERILOG` in `run.py` to point at any benchmark:
```python
VERILOG = 'benchmarks/shift_reg.v'
```

---

## Project Structure

```
OpenEDA/
├── benchmarks/            # Verilog netlists, .lib, .lef, .sdc, .drc
├── include/
│   ├── db/                # Design, Library, Pin, Net, GateInstance
│   ├── place/             # Placer, Legalizer, Floorplanner
│   ├── cts/               # CtsEngine
│   ├── route/             # RouteEngine, GlobalRouter, PdnGenerator
│   ├── analysis/          # DrcEngine, LvsEngine, ErcEngine, STA
│   ├── export/            # GdsExporter, SpefEngine
│   ├── synth/             # SynthEngine, GateSizer, LogicOptimizer
│   └── scripting/         # TCL engine, pybind11 bindings
├── src/                   # Implementation files (mirrors include/)
├── tests/                 # Python test suites (pytest)
├── cloud_workspace/       # Cloud job I/O directory
├── CMakeLists.txt
├── run.py                 # Full RTL-to-GDSII flow script
└── COMMERCIAL_ROADMAP.md  # Development roadmap
```

---

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Core engine | C++17 |
| Python bindings | pybind11 |
| Parallel routing | OpenMP |
| Scripting | Custom TCL engine |
| Synthesis | Yosys (subprocess) |
| PDK | SkyWater 130nm (sky130_fd_sc_hd) |
| GDSII viewer | KLayout / GDS3D |
| CI | GitHub Actions |

---

## License

Developed as a commercial-grade research and production platform for physical design automation.  
See `COMMERCIAL_ROADMAP.md` for the full development roadmap.
