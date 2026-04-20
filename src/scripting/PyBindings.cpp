#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// Include core headers
#include "db/Design.h"
#include "db/Library.h"
#include "db/LibertyParser.h"
#include "db/LefParser.h"
#include "parser/VerilogParser.h"
#include "place/PlaceEngine.h"
#include "route/RouteEngine.h"
#include "gui/GuiEngine.h"
#include "timer/Timer.h"
#include "cts/CtsEngine.h"
#include "route/PdnGenerator.h"
#include "export/GdsExporter.h"
#include "place/Legalizer.h"
#include "floorplan/Floorplanner.h"
#include "analysis/SpefEngine.h"
#include "analysis/EcoEngine.h"
#include "parser/SdcParser.h"

namespace py = pybind11;

void run_placement(Design* chip) {
    if (!chip) return;
    Timer physicsTimer(chip, chip->cellLibrary, nullptr);
    physicsTimer.buildGraph();
    PlaceEngine placer(chip, &physicsTimer);
    placer.runPlacement(*chip, chip->coreWidth, chip->coreHeight);
    Legalizer leg(chip, chip->coreWidth, chip->coreHeight);
    leg.run();
}

// Ensure the module name matches the target name in CMakeLists.txt (open_eda)
PYBIND11_MODULE(open_eda, m) {
    m.doc() = "OpenEDA Python Bindings using pybind11"; // Module docstring

    // 1. Expose Design Class
    py::class_<Design>(m, "Design")
        .def(py::init<>())
        .def("get_instance_count", [](const Design& d) {
            return d.instances.size();
        })
        // load_pdk: load a custom Liberty + LEF pair before calling load_verilog.
        // When a PDK is pre-loaded, load_verilog skips the hardcoded benchmarks/ library.
        .def("load_pdk", [](Design& d,
                             const std::string& libertyPath,
                             const std::string& lefPath) {
            // Allocate a persistent library on the heap
            Library* lib = new Library();
            d.cellLibrary = lib;

            LibertyParser libParser;
            libParser.parse(libertyPath, *lib);

            LefParser lefParser;
            lefParser.parse(lefPath, &d);

            py::print("[PDK] Loaded", (int)lib->cells.size(), "cells from", libertyPath);
        })
        .def("read_sdc", [](Design& d, const std::string& filename) {
            SdcParser parser;
            bool ok = parser.parse(filename, d.sdc);
            if (!ok) py::print("[SDC] Failed to load", filename);
            return ok;
        }, "Load SDC timing constraints from file")
        .def("get_clock_period", [](const Design& d) {
            return d.sdc.clockPeriod();
        }, "Return primary clock period from loaded SDC (ps)")
        .def("load_verilog", [](Design& d, const std::string& filename) {
            // If load_pdk() was already called, cellLibrary is non-null and populated.
            // In that case skip the default benchmarks/ library to avoid overwriting.
            bool pdkPreloaded = (d.cellLibrary != nullptr && !d.cellLibrary->cells.empty());

            static Library defaultLib;
            if (!pdkPreloaded) {
                d.cellLibrary = &defaultLib;

                auto createCell = [&](std::string name, double delay, bool isSeq) {
                    CellDef* c = new CellDef();
                    c->name = name; c->delay = delay; c->isSequential = isSeq;
                    c->width = 1.0; c->height = 1.0; c->leakagePower = 10.0;
                    return c;
                };

                CellDef* xor2 = createCell("XOR2", 2.5, false); xor2->width = 3.0;
                xor2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
                defaultLib.addCell(xor2);

                CellDef* and2 = createCell("AND2", 1.8, false); and2->width = 2.0;
                and2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
                defaultLib.addCell(and2);

                CellDef* or2 = createCell("OR2", 1.8, false); or2->width = 2.0;
                or2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
                defaultLib.addCell(or2);

                CellDef* dff = createCell("DFF", 1.0, true); dff->width = 4.0; dff->height = 2.0; dff->leakagePower = 50.0;
                dff->pins = {{"C", false, 0.004, -1.0, -1.0}, {"D", false, 0.004, -1.0, 1.0}, {"Q", true, 0.0, 1.0, 0.0}};
                defaultLib.addCell(dff);

                CellDef* buf1 = createCell("BUF", 0.5, false); buf1->width = 1.0;
                buf1->pins = {{"A", false, 0.004, -0.5, 0.0}, {"Y", true, 0.0, 0.5, 0.0}};
                defaultLib.addCell(buf1);

                CellDef* notGate = createCell("NOT", 0.3, false); notGate->width = 1.0;
                notGate->pins = {{"A", false, 0.004, -0.5, 0.0}, {"Y", true, 0.0, 0.5, 0.0}};
                defaultLib.addCell(notGate);

                CellDef* clkbuf = createCell("CLKBUF", 0.2, false); clkbuf->width = 2.0; clkbuf->height = 2.0; clkbuf->leakagePower = 100.0;
                clkbuf->pins = {{"A", false, 0.010, -1.0, 0.0}, {"Y", true, 0.0, 1.0, 0.0}};
                defaultLib.addCell(clkbuf);

                LibertyParser libParser;
                libParser.parse("benchmarks/simple.lib", defaultLib);

                LefParser lefParser;
                lefParser.parse("benchmarks/open_eda.lef", &d);
                lefParser.parse("benchmarks/sram.lef", &d);
            }

            Library& lib = *d.cellLibrary;
            VerilogParser parser;
            bool success = parser.read(filename, d, lib);
            if (success) {
                double coreSize = std::max(400.0, std::sqrt((double)d.instances.size()) * 30.0);
                d.coreWidth  = coreSize;
                d.coreHeight = coreSize;
            } else {
                py::print("Failed to load netlist from", filename);
            }
        });

    // 2. Expose Placement Wrapper
    m.def("run_placement", &run_placement, "Run initial placement");

    // 3. Expose RouteEngine Class
    py::class_<RouteEngine>(m, "RouteEngine")
        .def(py::init<>())
        .def("route", [](RouteEngine& router, Design* chip) {
            if (chip) router.runRouting(*chip, static_cast<int>(chip->coreWidth), static_cast<int>(chip->coreHeight));
        });

    // 4. Expose GuiEngine Class
    py::class_<GuiEngine>(m, "GuiEngine")
        .def(py::init<>())
        .def("show", [](GuiEngine& gui, Design* chip, RouteEngine* router, CtsEngine* ctsEngine) {
            if (gui.init(1280, 720, "Byteflow Visualizer (Python Shell)")) {
                gui.run(chip, router, ctsEngine);
            } else {
                py::print("[Error] Failed to initialize hardware-accelerated GUI.");
            }
        });

    // 5. Expose CtsEngine Class (Clock Tree Synthesis)
    py::class_<CtsEngine>(m, "CtsEngine")
        .def(py::init<>())
        .def("run_cts", &CtsEngine::runCTS, "Run Clock Tree Synthesis on a specific net");
        
    // 6. Expose PdnGenerator Class (Power Mesh)
    py::class_<PdnGenerator>(m, "PdnGenerator")
        .def(py::init<Design*>(), py::arg("design"))
        .def("run", &PdnGenerator::run, "Generate Power Delivery Network (VDD/VSS meshes and vias)");

    // 7. Expose GDSII Tape-Out Export
    m.def("export_gds", [](const std::string& filename, Design* design) {
        return GdsExporter::exportGds(filename, design);
    }, "Export design to GDSII binary file", py::arg("filename"), py::arg("design"));

    // 8. Expose Floorplanner Class
    py::class_<Floorplanner>(m, "Floorplanner")
        .def(py::init<>())
        .def("place_macros", &Floorplanner::placeMacros, "Place macro IPs");

    // 9. Expose SpefEngine Class
    py::class_<SpefEngine>(m, "SpefEngine")
        .def(py::init<>())
        .def("extract", &SpefEngine::extract, "Extract RC parasitics")
        .def("write_spef", [](SpefEngine& spef, const std::string& filename, Design* chip) {
            spef.writeSpef(filename, *chip);
        }, "Write SPEF file");

    // 10. Expose EcoResult struct + EcoEngine
    py::class_<EcoResult>(m, "EcoResult")
        .def_readonly("setup_fixed",       &EcoResult::setupFixed)
        .def_readonly("hold_fixed",        &EcoResult::holdFixed)
        .def_readonly("iterations",        &EcoResult::iterations)
        .def_readonly("final_setup_wns",   &EcoResult::finalSetupWns)
        .def_readonly("final_hold_wns",    &EcoResult::finalHoldWns)
        .def_readonly("final_setup_viols", &EcoResult::finalSetupViols)
        .def_readonly("final_hold_viols",  &EcoResult::finalHoldViols);

    py::class_<EcoEngine>(m, "EcoEngine")
        .def(py::init<>())
        .def("run_timing_closure", &EcoEngine::runTimingClosure,
             "Run ECO closure loop: gate sizing + buffer insertion",
             py::arg("design"), py::arg("timer"), py::arg("max_iter") = 10)
        .def("fix_setup_violations", &EcoEngine::fixSetupViolations,
             "Single-pass gate upsizing for setup", py::arg("design"), py::arg("timer"))
        .def("fix_hold_violations", &EcoEngine::fixHoldViolations,
             "Single-pass buffer insertion for hold", py::arg("design"), py::arg("timer"));

    // 11. Expose TimingSummary struct
    py::class_<TimingSummary>(m, "TimingSummary")
        .def_readonly("wns",             &TimingSummary::wns)
        .def_readonly("tns",             &TimingSummary::tns)
        .def_readonly("violations",      &TimingSummary::violations)
        .def_readonly("endpoints",       &TimingSummary::endpoints)
        .def_readonly("critical_path",   &TimingSummary::criticalPath)
        .def_readonly("hold_wns",        &TimingSummary::holdWns)
        .def_readonly("hold_tns",        &TimingSummary::holdTns)
        .def_readonly("hold_violations", &TimingSummary::holdViolations);

    // 11. Expose Timer (sign-off STA engine)
    py::class_<Timer>(m, "Timer")
        // Primary constructor: design only (library/spef resolved internally)
        .def(py::init([](Design* d) {
            return new Timer(d, d->cellLibrary, nullptr);
        }), py::arg("design"))
        // Full constructor: design + spef (for post-route sign-off)
        .def(py::init([](Design* d, SpefEngine* spef) {
            return new Timer(d, d->cellLibrary, spef);
        }), py::arg("design"), py::arg("spef"))
        .def("build_graph",         &Timer::buildGraph,
             "Build the timing graph from the current netlist")
        .def("update_timing",       &Timer::updateTiming,
             "Run forward + backward passes, compute slack")
        .def("set_clock_period",    &Timer::setClockPeriod,
             "Set clock period in picoseconds", py::arg("period_ps"))
        .def("set_input_delay",       &Timer::setInputDelay,
             "Set primary-input arrival offset (ps)", py::arg("delay_ps"))
        .def("set_output_delay",      &Timer::setOutputDelay,
             "Set primary-output budget reduction (ps)", py::arg("delay_ps"))
        .def("set_clock_uncertainty", &Timer::setClockUncertainty,
             "Set clock uncertainty (jitter+skew) in ps", py::arg("unc_ps"))
        .def("set_clock_latency",     &Timer::setClockLatency,
             "Set clock source latency in ps", py::arg("lat_ps"))
        .def("get_wns",                  &Timer::getWNS,
             "Setup WNS — worst setup slack across all endpoints (ps)")
        .def("get_tns",                  &Timer::getTNS,
             "Setup TNS — sum of all setup violations (ps)")
        .def("get_violation_count",      &Timer::getViolationCount,
             "Number of endpoints with negative setup slack")
        .def("get_hold_wns",             &Timer::getHoldWNS,
             "Hold WNS — worst hold slack across all FF D-pins (ps)")
        .def("get_hold_tns",             &Timer::getHoldTNS,
             "Hold TNS — sum of all hold violations (ps)")
        .def("get_hold_violation_count", &Timer::getHoldViolationCount,
             "Number of FF D-pins with negative hold slack")
        .def("get_summary",         &Timer::getSummary,
             "Return a TimingSummary with WNS, TNS, violation count")
        .def("report_critical_path",&Timer::reportCriticalPath,
             "Print the critical path breakdown to stdout")
        .def("report_all_endpoints",&Timer::reportAllEndpoints,
             "Print slack for every timing endpoint");
}
