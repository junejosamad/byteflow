#include <iostream>
#include <iomanip>
#include <cmath>
#include <ctime>  // Needed for random seed
#include "db/Design.h"
#include "db/Library.h"
#include "parser/VerilogParser.h"
#include "analysis/StaEngine.h"
#include "place/PlaceEngine.h"
#include "place/Legalizer.h" // <--- Include this
#include "gui/GuiEngine.h"
#include "route/PdnGenerator.h" // <--- Include this
#include "util/SvgExporter.h"
#include "route/RouteEngine.h"
#include "analysis/LogicOptimizer.h"
#include "analysis/PowerEngine.h" 
#include "analysis/CtsEngine.h" // <--- LEVEL 4 INCLUDE: Clock Tree Synthesis
#include "analysis/DrcEngine.h"
#include "db/LibertyParser.h" // <--- Include this
#include "db/LefParser.h"     // <--- LEF Physical Geometry
#include "util/VerilogWriter.h" // <--- LEVEL 8
#include "util/DefExporter.h" // <--- LEVEL 9
#include "util/ScriptExporter.h" // <--- LEVEL 10
#include "gui/GuiEngine.h"
#include "export/GdsExporter.h"
#include "timer/Timer.h"
#include "analysis/SpefEngine.h"  // PHASE 5: RC Parasitic Extraction

int main(int argc, char* argv[]) {
    std::cout << "--- OpenEDA System Initializing ---\n";

    if (argc < 2) {
        std::cout << "Usage: open_eda.exe <verilog_file>\n";
        return 1;
    }

    // CRITICAL: Initialize Random Seed for Placement
    std::srand((unsigned int)std::time(nullptr));

    // 1. SETUP LIBRARY (The Rulebook)
    Library lib;

    // Helper to create basic cell info
    auto createCell = [&](std::string name, double delay, bool isSeq) {
        CellDef* c = new CellDef();
        c->name = name; c->delay = delay; c->isSequential = isSeq;
        c->width = 1.0; c->height = 1.0;

        // LEVEL 3: Default Leakage Power (10 nW)
        c->leakagePower = 10.0;
        return c;
        };

    // XOR2 (Inputs Left, Output Center)
    CellDef* xor2 = createCell("XOR2", 2.5, false); xor2->width = 3.0;
    xor2->pins = {
        {"A", false, 0.004, -1.0, 0.0},  // Left (dx=-1)
        {"B", false, 0.004,  1.0, 0.0},  // Right (dx=1)
        {"Y", true,  0.0,    0.0, 0.0}   // Center
    };
    lib.addCell(xor2);

    // AND2
    CellDef* and2 = createCell("AND2", 1.8, false); and2->width = 2.0;
    and2->pins = {
        {"A", false, 0.004, -1.0, 0.0},
        {"B", false, 0.004,  1.0, 0.0},
        {"Y", true,  0.0,    0.0, 0.0}
    };
    lib.addCell(and2);

    // OR2
    CellDef* or2 = createCell("OR2", 1.8, false); or2->width = 2.0;
    or2->pins = {
        {"A", false, 0.004, -1.0, 0.0},
        {"B", false, 0.004,  1.0, 0.0},
        {"Y", true,  0.0,    0.0, 0.0}
    };
    lib.addCell(or2);

    // DFF (Inputs Left, Output Right)
    CellDef* dff = createCell("DFF", 1.0, true); dff->width = 4.0; dff->height = 2.0;
    // LEVEL 3: DFFs leak more power (50 nW)
    dff->leakagePower = 50.0;
    dff->pins = {
        {"C", false, 0.004, -1.0, -1.0}, // Bottom-Left
        {"D", false, 0.004, -1.0,  1.0}, // Top-Left
        {"Q", true,  0.0,    1.0,  0.0}  // Right
    };
    lib.addCell(dff);

    // BUFFER (Input Left, Output Right)
    CellDef* buf1 = createCell("BUF", 0.5, false); buf1->width = 1.0;
    buf1->pins = {
        {"A", false, 0.004, -0.5, 0.0},
        {"Y", true,  0.0,    0.5, 0.0}
    };
    lib.addCell(buf1);

    // NOT (Inverter)
    CellDef* notGate = createCell("NOT", 0.3, false); notGate->width = 1.0;
    notGate->pins = {
        {"A", false, 0.004, -0.5, 0.0},
        {"Y", true,  0.0,    0.5, 0.0}
    };
    lib.addCell(notGate);

    // LEVEL 4: CLOCK BUFFER (High Power, Low Delay)
    CellDef* clkbuf = createCell("CLKBUF", 0.2, false);
    clkbuf->width = 2.0; clkbuf->height = 2.0;
    clkbuf->leakagePower = 100.0; // High leakage
    clkbuf->pins = {
        {"A", false, 0.010, -1.0, 0.0}, // High input cap
        {"Y", true,  0.0,    1.0, 0.0}  // High drive strength
    };
    lib.addCell(clkbuf);

    // 2. LOAD PHYSICS (Liberty)
    LibertyParser libParser;
    libParser.parse("benchmarks/simple.lib", lib);

    // 3. PARSE DESIGN (first create the Design so LEF can populate it)
    Design chip;

    // 2b. LOAD PHYSICAL GEOMETRY (LEF)
    LefParser lefParser;
    lefParser.parse("benchmarks/open_eda.lef", &chip);

    // 3. PARSE NETLIST
    VerilogParser parser;
    std::string filename = argv[1];

    if (parser.read(filename, chip, lib)) {
        std::cout << "Design Loaded Successfully.\n";



        // 3. INITIALIZE ENGINES
        // Auto-scale core area based on cell count
        double coreSize = std::max(400.0, std::sqrt((double)chip.instances.size()) * 30.0);
        chip.coreWidth = coreSize;
        chip.coreHeight = coreSize;
        std::cout << "\n[Config] Core area: " << coreSize << " x " << coreSize 
                  << " (" << chip.instances.size() << " cells)\n";

        StaEngine timer;
        Timer physicsTimer(&chip, &lib, nullptr);
        physicsTimer.buildGraph();
        PlaceEngine placer(&chip, &physicsTimer);
        RouteEngine router;
        LogicOptimizer opt;
        PowerEngine power;
        CtsEngine cts(&chip);       // <--- Level 4 Engine
        SvgExporter viewer;

        // 4. PRE-PLACEMENT
        // Run Initial Timing
        timer.updateTiming(chip);
        timer.reportTiming(chip);
        timer.checkConstraints(chip, 10.0);

        // Snapshot A: Random Initial State
        std::cout << "\n[Visualizer] Capturing Random Initial State...\n";
        viewer.exportLayout(chip, "layout_initial.svg", coreSize, coreSize);

        // 5. PLACEMENT
        placer.runPlacement(chip, coreSize, coreSize);

        // 5b. LEGALIZATION (Snap to Rows)
        Legalizer leg(&chip, coreSize, coreSize);
        leg.run();

        // Snapshot B: Placed State
        std::cout << "\n[Visualizer] Capturing Placed State...\n";
        viewer.exportLayout(chip, "layout_placed.svg", coreSize, coreSize);

        // 6. CLOCK TREE SYNTHESIS (Level 4) 
        // This inserts a Clock Buffer at the center of mass of the registers
        cts.runCTS(chip, lib);

        // 6b. PDN GENERATION (Power Grid)
        PdnGenerator pdn(&chip, coreSize, coreSize);
        pdn.run();

        // 7. GLOBAL ROUTING (Pass 1)
        router.runRouting(chip, (int)coreSize, (int)coreSize);

        // 8. LOGIC OPTIMIZATION (Buffer Insertion)
        // TEMPORARILY DISABLED: Causes post-route overlap shorts
        /*
        opt.fixTiming(chip, lib);

        // 9. INCREMENTAL ROUTING (Route the new buffers)
        std::cout << "\n[Router] Re-routing modified nets...\n";
        router.runRouting(chip, 400, 400);
        */

        // 10. PHASE 5: RC PARASITIC EXTRACTION
        SpefEngine spef;
        spef.extract(chip);
        std::string spefName = filename.substr(0, filename.find_last_of(".")) + ".spef";
        spef.writeSpef(spefName, chip);

        // 11. FINAL SIGN-OFF STA (with NLDM + Elmore Physics)
        std::cout << "\n--- Final Sign-off STA (Phase 5: NLDM + Elmore) ---\n";
        Timer finalTimer(&chip, &lib, &spef);
        finalTimer.buildGraph();
        finalTimer.setClockPeriod(1000.0); // 1 GHz target
        finalTimer.updateTiming();
        finalTimer.reportCriticalPath();

        // Legacy STA report (for comparison)
        timer.updateTiming(chip);
        timer.reportTiming(chip);

        // Level 3: Power Analysis
        power.reportPower(chip, 1.0, 1000.0);

        // 12. PHYSICAL VERIFICATION (DRC)
        DrcEngine drc;
        drc.runDRC(chip);

        // 13. EXPORT RESULTS
        VerilogWriter writer;
        std::string outName = filename.substr(0, filename.find_last_of(".")) + "_routed.v";
        writer.write(chip, outName);

        // 14. EXPORT DEF (Industry Standard)
        DefExporter defExporter;
        std::string defName = filename.substr(0, filename.find_last_of(".")) + ".def";
        defExporter.writeDEF(defName, &chip, coreSize, coreSize);

        // 15. EXPORT GDSII (Silicon Tape-Out)
        std::string gdsName = filename.substr(0, filename.find_last_of(".")) + "_layout.gds";
        GdsExporter::exportGds(gdsName, &chip);

        // 16. EXPORT PYTHON SCRIPT
        ScriptExporter scriptWriter;
        std::string pyName = filename.substr(0, filename.find_last_of(".")) + "_load.py";
        scriptWriter.write(chip, pyName);

        // Snapshot C: Final Routed State
        std::cout << "\n[Visualizer] Capturing Final Routed State...\n";
        viewer.exportLayout(chip, "layout_routed.svg", coreSize, coreSize);

        if (!chip.instances.empty()) {
            GateInstance* g1 = chip.instances[0];
            std::cout << "\nFinal loc of " << g1->name << ": (" << g1->x << ", " << g1->y << ")\n";
        }

        std::cout << "\n=== LAUNCHING BYTEFLOW VISUALIZER ===\n";
        GuiEngine gui;
        if (gui.init(1280, 720, "Byteflow Visualizer - 32-Bit ALU")) {
            // Hand the fully routed design over to the GPU
            gui.run(&chip, &router); 
        } else {
            std::cerr << "[Error] Failed to initialize hardware-accelerated GUI.\n";
        }

    }
    else {
        std::cout << "Parsing Failed.\n";
        return 1;
    }

    return 0;
}
