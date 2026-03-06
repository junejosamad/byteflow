# OpenEDA — Byteflow Silicon Compiler 🚀

**A from-scratch C++ RTL-to-GDSII Physical Design Engine**

OpenEDA is a fully automated VLSI physical design flow built from the ground up in C++17. It takes structural Verilog netlists and executes the complete chip implementation pipeline — placement, clock tree synthesis, power delivery, 3D routing, and GDSII tape-out — producing foundry-ready binary layout files.

Built with a hardware-accelerated OpenGL visualizer and a Python scripting shell for interactive exploration.

---

## ⚡ Pipeline Overview

```
Verilog Netlist (.v)
        │
   ┌────▼─────┐
   │  Parser   │  LEF geometry · Liberty timing · Structural Verilog
   └────┬──────┘
        │
   ┌────▼──────────────────┐
   │  Static Timing (STA)  │  Topological delay propagation · Critical path analysis
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Global Placement     │  Simulated Annealing · Timing-driven wirelength minimization
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Clock Tree Synthesis │  Method of Means & Medians · Auto buffer insertion
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  Power Delivery (PDN) │  M1 rails · M3/M4 orthogonal stripe mesh · Via arrays
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  3D A* Router         │  Multi-layer negotiation · Rip-up & reroute · Jog insertion
   └────┬──────────────────┘
        │
   ┌────▼──────────────────┐
   │  GDSII Tape-Out       │  Big-endian binary · Hierarchical cells · SREF instantiation
   └────┬──────────────────┘
        │
        ▼
   output.gds  ←  Foundry-ready layout
```

---

## 🧠 Engine Modules

### 1. Library Parsing (LEF / Liberty)
Dynamically parses `.lib` for timing arcs and `.lef` for microscopic pin geometry, macro bounding boxes, and physical dimensions.

### 2. Static Timing Analysis (STA)
Topological delay engine that traces critical paths and computes arrival/required times across the full timing graph.

### 3. Global Placement (Simulated Annealing)
Timing-driven physics engine that minimizes total wirelength across the silicon canvas, with row-snapping legalization to enforce manufacturing constraints.

### 4. Clock Tree Synthesis (CTS)
Recursive Method of Means and Medians tree builder with automatic `CLKBUF` insertion and legalization for zero-skew clock distribution.

### 5. Power Delivery Network (PDN)
Generates M1 standard cell power rails, vertical M3 stripes, horizontal M4 stripes, and M3→M1 / M4→M3 via arrays for robust VDD/VSS delivery.

### 6. 3D Detailed Router
Multi-layer congestion-aware **A\* Maze Router** with:
- **PathFinder negotiation** — history-based penalty ramp for rip-up and reroute
- **Pin whitelist** — correctly identifies legal shared-pin junctions (e.g. CTS buffer split points) to eliminate false-positive DRC shorts
- **Jog insertion** — post-processing pass to side-step Layer 1 adjacency conflicts
- **Layer cost model** — M1 expensive local, M2 vertical highway, M3 horizontal highway
- **OpenMP acceleration** — parallelized per-net A* search

### 7. GDSII Tape-Out Export
Generates foundry-ready **GDSII II** binary files with:
- **Big-endian byte swapping** for x86 compatibility
- **IBM System/360 Real8** floating-point conversion for UNITS records
- **Hierarchical cell structures** — each standard cell type gets its own STRUCTURE with bounding box and pin rectangles
- **SREF instantiation** — places cells at legalized coordinates without polygon duplication
- **GDS layer mapping** — M1→68, M2→69, M3→70, M4→71, VIA12→50, VIA23→51, VIA34→52
- **1D→2D wire expansion** — converts centerline segments to closed-loop copper rectangles
- **PDN export** — power grid stripes and via arrays on dedicated metal layers

### 8. Hardware-Accelerated GUI
OpenGL + GLFW real-time visualizer showing placed cells, clock tree topology, power grid, and routed wires with layer coloring.

### 9. Python Scripting Shell
Pybind11-based interactive shell exposing the full engine API:

```python
import open_eda

chip = open_eda.Design()
chip.load_verilog("benchmarks/shift_reg.v")

open_eda.run_placement(chip)

cts = open_eda.CtsEngine()
cts.run_cts(chip, "clk")

pdn = open_eda.PdnGenerator(chip)
pdn.run()

router = open_eda.RouteEngine()
router.route(chip)

open_eda.export_gds("output.gds", chip)
```

---

## 📊 Benchmarks

| Metric | Value |
|--------|-------|
| **Test Design** | Shift Register / 32-bit ALU |
| **Routing Layers** | 4 (M1 local, M2 vertical, M3/M4 highways) |
| **Routing Convergence** | Iteration 1 — 0 DRC violations |
| **Multithreading** | OpenMP parallelized A* search |
| **GDSII Output** | ~210 KB, hierarchical with SREF cells |

---

## 🛠️ Build & Run

### Prerequisites
- C++17 compiler (GCC / Clang / MSVC)
- CMake 3.10+
- Python 3.x (for scripting shell)

### Build as Python Module
```bash
mkdir build && cd build
cmake .. -DBUILD_PYTHON_MODULE=ON
cmake --build . --config Release
```

> The build automatically copies the compiled `.pyd` / `.so` to the project root for direct `import open_eda`.

### Build as Standalone Executable
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Run the Full Pipeline
```bash
python test_open_eda.py
```

This executes all 7 stages: Parse → Place → CTS → PDN → Route → GDSII Export → GUI.

---

## 📁 Project Structure

```
OpenEDA/
├── benchmarks/            # Verilog netlists, LEF, Liberty
├── include/
│   ├── db/                # Design, Library, Pin, Net, GateInstance
│   ├── route/             # RouteEngine, PdnGenerator headers
│   └── ...
├── src/
│   ├── db/                # Liberty/LEF parsers, design database
│   ├── place/             # Simulated annealing placer
│   ├── cts/               # Clock tree synthesis engine
│   ├── route/             # A* 3D router, PDN generator
│   ├── export/            # GDSII binary exporter
│   ├── gui/               # OpenGL + ImGui visualizer
│   ├── timer/             # Static timing analysis
│   ├── scripting/         # Pybind11 Python bindings
│   └── analysis/          # DRC, logic optimizer, power analysis
├── CMakeLists.txt
├── test_open_eda.py       # Interactive Python test script
└── README.md
```

---

## 🔬 Technical Deep Dive: The 3D Router

The routing engine uses a **bounding-box optimized A\* search** across a 3D quantized grid (`gridW × gridH × 4 layers`). Key techniques:

- **Generational arrays** — O(1) per-search reset instead of O(N) memset
- **PathFinder negotiation** — exponentially escalating history penalties force flatlined conflicts to rip-up and reroute
- **Pin whitelist bypass** — shared buffer pins (e.g. CTS clock buffer split points) are identified as legal junctions and excluded from conflict counting, eliminating false-positive DRC shorts
- **Pattern routing fast-path** — 2-pin nets attempt L-shaped / Z-shaped routes before falling back to full A*
- **Dynamic bounding box** — search area expands with iteration count to allow increasingly creative detours

---

## 📜 License

This project is developed as an educational and research platform for physical design automation.
