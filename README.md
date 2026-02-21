# OpenEDA 🚀
**A C++ RTL-to-GDSII Physical Design Engine & Silicon Compiler**

OpenEDA is a custom, fully automated physical design flow built from scratch in C++. It takes behavioral Verilog RTL, synthesizes it, and executes a complete VLSI physical design flow to generate industry-standard DEF layouts and vector visualizations.

![Final Routed 32-bit ALU](benchmarks/layout_routed.svg) *(Insert a screenshot of your 32-bit ALU routing here)*

## 🧠 System Architecture

The engine is built around a robust, multi-stage compilation pipeline mirroring industrial tools like Cadence Innovus and Synopsys IC Compiler.

1. **Logic Synthesis (via Yosys):** Translates behavioral Verilog (`q <= a + b`) into structural standard-cell netlists.
2. **Library Parsing (LEF/LIB):** Dynamically parses `.lib` for timing parameters and `.lef` for physical, microscopic pin geometry and macro bounding boxes.
3. **Static Timing Analysis (STA):** Topological delay engine that traces critical paths and verifies setup/hold constraints.
4. **Global Placement:** A Simulated Annealing physics engine that minimizes total wirelength across the silicon canvas.
5. **Legalization:** Row-snapping and overlap-removal to enforce manufacturing constraints.
6. **Clock Tree Synthesis (CTS):** Recursive H-Tree generation with automatic buffer insertion for zero-skew clock distribution.
7. **Power Delivery Network (PDN):** Orthogonal M1/M2 power stripe generation (VDD/VSS).
8. **3D Global & Detailed Routing:** A multi-layer, congestion-aware A* Maze Router featuring rip-up and reroute negotiation, layer promotion penalties, and a post-processing jog-insertion pass for 99.9% DRC-clean layouts.
9. **DEF Export:** Generates Foundry-ready Design Exchange Format (`.def`) files.

## 📊 Benchmarks & Performance

OpenEDA has been successfully tested on high-density logic blocks, including a fully functional **32-Bit Arithmetic Logic Unit (ALU)**.

* **Design:** 32-bit RISC-style ALU
* **Gate Count:** ~800 Standard Cells
* **Grid Resolution:** 856 x 856 DBU
* **Routing Layers:** 3 (M1 Local, M2 Vertical, M3 Horizontal)
* **DRC Cleanliness:** 99.99% (resolved 4,000+ initial congestion shorts down to 1 via detailed jog insertion)
* **Critical Path:** 59.70 ns (Setup MET)

## 🛠️ Build & Run

### Prerequisites
* C++17 Compiler (GCC/Clang/MSVC)
* CMake (3.10+)
* Yosys (for RTL synthesis)

### Compilation
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Running the Flow
You can execute the entire flow (Synthesis -> DEF Generation) using the provided batch script:

```bash
./run_flow.bat benchmarks/alu_32bit.v
```

## 🔬 Under the Hood: The 3D Router
The routing engine utilizes a bounding-box optimized A* search across a 3D quantized grid. To resolve routing deadlocks (symmetric oscillation), it applies history-based negotiation penalties (PathFinder algorithm) and implements microscopic "jog" insertion to side-step Layer 1 adjacency conflicts without breaking pin connectivity.
