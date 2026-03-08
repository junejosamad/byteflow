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
        .def("load_verilog", [](Design& d, const std::string& filename) {
            // CRITICAL FIX: Library must outlive this lambda, so we allocate it statically or on the heap
            static Library lib;
            d.cellLibrary = &lib; // NEW: Give Design access to fetch CLKBUF standard cell later
            // Provide dummy base cells just like main.cpp
            auto createCell = [&](std::string name, double delay, bool isSeq) {
                CellDef* c = new CellDef();
                c->name = name; c->delay = delay; c->isSequential = isSeq;
                c->width = 1.0; c->height = 1.0;
                c->leakagePower = 10.0;
                return c;
            };

            CellDef* xor2 = createCell("XOR2", 2.5, false); xor2->width = 3.0;
            xor2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
            lib.addCell(xor2);

            CellDef* and2 = createCell("AND2", 1.8, false); and2->width = 2.0;
            and2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
            lib.addCell(and2);

            CellDef* or2 = createCell("OR2", 1.8, false); or2->width = 2.0;
            or2->pins = {{"A", false, 0.004, -1.0, 0.0}, {"B", false, 0.004, 1.0, 0.0}, {"Y", true, 0.0, 0.0, 0.0}};
            lib.addCell(or2);

            CellDef* dff = createCell("DFF", 1.0, true); dff->width = 4.0; dff->height = 2.0; dff->leakagePower = 50.0;
            dff->pins = {{"C", false, 0.004, -1.0, -1.0}, {"D", false, 0.004, -1.0, 1.0}, {"Q", true, 0.0, 1.0, 0.0}};
            lib.addCell(dff);

            CellDef* buf1 = createCell("BUF", 0.5, false); buf1->width = 1.0;
            buf1->pins = {{"A", false, 0.004, -0.5, 0.0}, {"Y", true, 0.0, 0.5, 0.0}};
            lib.addCell(buf1);

            CellDef* notGate = createCell("NOT", 0.3, false); notGate->width = 1.0;
            notGate->pins = {{"A", false, 0.004, -0.5, 0.0}, {"Y", true, 0.0, 0.5, 0.0}};
            lib.addCell(notGate);

            CellDef* clkbuf = createCell("CLKBUF", 0.2, false); clkbuf->width = 2.0; clkbuf->height = 2.0; clkbuf->leakagePower = 100.0;
            clkbuf->pins = {{"A", false, 0.010, -1.0, 0.0}, {"Y", true, 0.0, 1.0, 0.0}};
            lib.addCell(clkbuf);

            LibertyParser libParser;
            libParser.parse("benchmarks/simple.lib", lib);

            LefParser lefParser;
            lefParser.parse("benchmarks/open_eda.lef", &d);
            lefParser.parse("benchmarks/sram.lef", &d);

            VerilogParser parser;
            bool success = parser.read(filename, d, lib);
            if (success) {
                double coreSize = std::max(400.0, std::sqrt((double)d.instances.size()) * 30.0);
                d.coreWidth = coreSize;
                d.coreHeight = coreSize;
            } else {
                py::print("Failed to load standard cell library or netlist.");
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
}
