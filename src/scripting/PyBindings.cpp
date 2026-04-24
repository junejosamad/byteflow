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
#include "analysis/TimingReporter.h"
#include "analysis/DrcEngine.h"
#include "analysis/LvsEngine.h"
#include "analysis/ErcEngine.h"
#include "synthesis/SynthEngine.h"
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
    // Mark all instances as placed after SA + legalization complete
    for (GateInstance* inst : chip->instances)
        inst->isPlaced = true;
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

    // 12. TimingReporter — structured sign-off reports
    py::class_<PathStep>(m, "PathStep")
        .def_readonly("inst_name",    &PathStep::instName)
        .def_readonly("pin_name",     &PathStep::pinName)
        .def_readonly("cell_type",    &PathStep::cellType)
        .def_readonly("gate_delay",   &PathStep::gateDelay)
        .def_readonly("wire_delay",   &PathStep::wireDelay)
        .def_readonly("arrival_time", &PathStep::arrivalTime)
        .def_readonly("slack",        &PathStep::slack);

    py::class_<PathReport>(m, "PathReport")
        .def_readonly("startpoint",    &PathReport::startpoint)
        .def_readonly("endpoint",      &PathReport::endpoint)
        .def_readonly("type",          &PathReport::type)
        .def_readonly("slack",         &PathReport::slack)
        .def_readonly("arrival_time",  &PathReport::arrivalTime)
        .def_readonly("required_time", &PathReport::requiredTime)
        .def_readonly("steps",         &PathReport::steps);

    py::class_<SlackBin>(m, "SlackBin")
        .def_readonly("lo",    &SlackBin::lo)
        .def_readonly("hi",    &SlackBin::hi)
        .def_readonly("count", &SlackBin::count)
        .def_readonly("label", &SlackBin::label);

    py::class_<TimingReporter>(m, "TimingReporter")
        .def(py::init([](Timer* t, Design* d) {
            return new TimingReporter(*t, *d);
        }), py::arg("timer"), py::arg("design"),
            "Construct a reporter from a fully-updated Timer and its Design")
        .def("get_top_paths",
             &TimingReporter::getTopPaths,
             "Return list of N worst-slack PathReport objects",
             py::arg("n") = 5)
        .def("get_slack_histogram",
             &TimingReporter::getSlackHistogram,
             "Return list of SlackBin objects (endpoint count per bucket)",
             py::arg("bins") = 10)
        .def("format_summary",
             &TimingReporter::formatSummary,
             "Return sign-off summary block as a string")
        .def("format_path",
             &TimingReporter::formatPath,
             "Return formatted text breakdown for one PathReport",
             py::arg("path"))
        .def("format_slack_histogram",
             &TimingReporter::formatSlackHistogram,
             "Return ASCII bar-chart histogram as a string",
             py::arg("bins") = 10)
        .def("format_all_endpoints",
             &TimingReporter::formatAllEndpoints,
             "Return tabular endpoint slack table as a string")
        .def("format_cdc_report",
             &TimingReporter::formatCdcReport,
             "Return clock domain crossing summary as a string")
        .def("write_text_report",
             &TimingReporter::writeTextReport,
             "Write full .rpt text file; returns True on success",
             py::arg("filename"))
        .def("write_html_report",
             &TimingReporter::writeHtmlReport,
             "Write self-contained HTML sign-off page; returns True on success",
             py::arg("filename"));

    // 13. DRC Engine — physical verification
    py::enum_<DrcViolationType>(m, "DrcViolationType")
        .value("SHORT",        DrcViolationType::SHORT)
        .value("MIN_SPACING",  DrcViolationType::MIN_SPACING)
        .value("MIN_WIDTH",    DrcViolationType::MIN_WIDTH)
        .value("MIN_AREA",     DrcViolationType::MIN_AREA)
        .value("VIA_ENCLOSURE",DrcViolationType::VIA_ENCLOSURE)
        .export_values();

    py::class_<DrcViolation>(m, "DrcViolation")
        .def_readonly("type",    &DrcViolation::type)
        .def_readonly("layer",   &DrcViolation::layer)
        .def_readonly("x1",      &DrcViolation::x1)
        .def_readonly("y1",      &DrcViolation::y1)
        .def_readonly("x2",      &DrcViolation::x2)
        .def_readonly("y2",      &DrcViolation::y2)
        .def_readonly("net1",    &DrcViolation::net1)
        .def_readonly("net2",    &DrcViolation::net2)
        .def_readonly("message", &DrcViolation::message);

    py::class_<LayerRule>(m, "LayerRule")
        .def_readonly("layer_idx",   &LayerRule::layerIdx)
        .def_readonly("name",        &LayerRule::name)
        .def_readonly("min_width",   &LayerRule::minWidth)
        .def_readonly("min_spacing", &LayerRule::minSpacing)
        .def_readonly("min_area",    &LayerRule::minArea);

    py::class_<ViaRule>(m, "ViaRule")
        .def_readonly("from_layer",  &ViaRule::fromLayer)
        .def_readonly("to_layer",    &ViaRule::toLayer)
        .def_readonly("name",        &ViaRule::name)
        .def_readonly("enclosure",   &ViaRule::enclosure)
        .def_readonly("via_size",    &ViaRule::viaSize);

    py::class_<DrcRuleDeck>(m, "DrcRuleDeck")
        .def(py::init<>())
        .def_static("sky130",        &DrcRuleDeck::sky130,
                    "Return built-in sky130_hd rule deck")
        .def("load_from_file",       &DrcRuleDeck::loadFromFile,
             "Parse a .drc rule deck file (values in nm); returns True on success",
             py::arg("filename"))
        .def_readonly("layer_rules", &DrcRuleDeck::layerRules)
        .def_readonly("via_rules",   &DrcRuleDeck::viaRules);

    py::class_<DrcReport>(m, "DrcReport")
        .def_readonly("violations",  &DrcReport::violations)
        .def("total_count",   &DrcReport::totalCount,   "Total violation count")
        .def("short_count",   &DrcReport::shortCount,   "Short circuit count")
        .def("spacing_count", &DrcReport::spacingCount, "Min-spacing violation count")
        .def("width_count",   &DrcReport::widthCount,   "Min-width violation count")
        .def("area_count",    &DrcReport::areaCount,    "Min-area violation count")
        .def("print",         &DrcReport::print,
             "Print DRC summary to stdout",
             py::arg("max_print") = 30);

    py::class_<DrcEngine>(m, "DrcEngine")
        .def(py::init<>())
        .def("run_drc",
             py::overload_cast<Design*>(&DrcEngine::runDrc),
             "Run DRC with sky130 built-in rule deck",
             py::arg("design"))
        .def("run_drc",
             py::overload_cast<Design*, const DrcRuleDeck&>(&DrcEngine::runDrc),
             "Run DRC with a custom rule deck",
             py::arg("design"), py::arg("rules"));

    // 14. LVS Engine — layout vs. schematic
    py::enum_<LvsMismatchType>(m, "LvsMismatchType")
        .value("UNPLACED_INSTANCE", LvsMismatchType::UNPLACED_INSTANCE)
        .value("UNCONNECTED_PIN",   LvsMismatchType::UNCONNECTED_PIN)
        .value("UNROUTED_NET",      LvsMismatchType::UNROUTED_NET)
        .value("OPEN_CIRCUIT",      LvsMismatchType::OPEN_CIRCUIT)
        .export_values();

    py::class_<LvsMismatch>(m, "LvsMismatch")
        .def_readonly("type",      &LvsMismatch::type)
        .def_readonly("inst_name", &LvsMismatch::instName)
        .def_readonly("net_name",  &LvsMismatch::netName)
        .def_readonly("pin_name",  &LvsMismatch::pinName)
        .def_readonly("message",   &LvsMismatch::message);

    py::class_<LvsReport>(m, "LvsReport")
        .def_readonly("mismatches",         &LvsReport::mismatches)
        .def_readonly("instance_count",     &LvsReport::instanceCount)
        .def_readonly("net_count",          &LvsReport::netCount)
        .def_readonly("routed_net_count",   &LvsReport::routedNetCount)
        .def_readonly("total_pin_count",    &LvsReport::totalPinCount)
        .def_readonly("connected_pin_count",&LvsReport::connectedPinCount)
        .def("clean",                &LvsReport::clean,
             "True if no mismatches found")
        .def("total_count",          &LvsReport::totalCount,
             "Total mismatch count")
        .def("unplaced_count",       &LvsReport::unplacedCount,
             "Unplaced instance count")
        .def("unconnected_pin_count",&LvsReport::unconnectedPinCount,
             "Floating pin count")
        .def("unrouted_count",       &LvsReport::unroutedCount,
             "Unrouted net count")
        .def("open_circuit_count",   &LvsReport::openCircuitCount,
             "Open circuit count")
        .def("print",                &LvsReport::print,
             "Print LVS report to stdout",
             py::arg("max_print") = 30);

    py::class_<LvsEngine>(m, "LvsEngine")
        .def(py::init<>())
        .def("run_lvs", &LvsEngine::runLvs,
             "Run LVS: placement, pin connectivity, net routing, physical coverage",
             py::arg("design"));

    // ── ERC Engine ───────────────────────────────────────────────
    py::enum_<ErcViolationType>(m, "ErcViolationType")
        .value("FLOATING_INPUT",  ErcViolationType::FLOATING_INPUT)
        .value("MULTIPLE_DRIVER", ErcViolationType::MULTIPLE_DRIVER)
        .value("NO_POWER_PIN",    ErcViolationType::NO_POWER_PIN)
        .export_values();

    py::class_<ErcViolation>(m, "ErcViolation")
        .def_readonly("type",      &ErcViolation::type)
        .def_readonly("inst_name", &ErcViolation::instName)
        .def_readonly("net_name",  &ErcViolation::netName)
        .def_readonly("pin_name",  &ErcViolation::pinName)
        .def_readonly("message",   &ErcViolation::message);

    py::class_<ErcReport>(m, "ErcReport")
        .def_readonly("violations",      &ErcReport::violations)
        .def_readonly("instance_count",  &ErcReport::instanceCount)
        .def_readonly("net_count",       &ErcReport::netCount)
        .def_readonly("pin_count",       &ErcReport::pinCount)
        .def("clean",                &ErcReport::clean,
             "True when there are no ERC violations")
        .def("total_count",          &ErcReport::totalCount,
             "Total number of ERC violations")
        .def("floating_input_count", &ErcReport::floatingInputCount,
             "Number of FLOATING_INPUT violations")
        .def("multiple_driver_count",&ErcReport::multipleDriverCount,
             "Number of MULTIPLE_DRIVER violations")
        .def("no_power_pin_count",   &ErcReport::noPowerPinCount,
             "Number of NO_POWER_PIN violations")
        .def("print",                &ErcReport::print,
             "Print ERC report to stdout",
             py::arg("max_print") = 30);

    py::class_<ErcEngine>(m, "ErcEngine")
        .def(py::init<>())
        .def("run_erc", &ErcEngine::runErc,
             "Run ERC: floating inputs, multiple drivers, power pin connectivity",
             py::arg("design"));

    // ── Synthesis Engine ─────────────────────────────────────────
    py::class_<SynthResult>(m, "SynthResult")
        .def_readonly("success",        &SynthResult::success)
        .def_readonly("output_netlist", &SynthResult::outputNetlist)
        .def_readonly("cell_count",     &SynthResult::cellCount)
        .def_readonly("log",            &SynthResult::log)
        .def_readonly("error_message",  &SynthResult::errorMessage);

    py::class_<SynthEngine>(m, "SynthEngine")
        .def(py::init<>())
        .def("synthesize", &SynthEngine::synthesize,
             "Synthesize RTL Verilog to structural netlist using Yosys",
             py::arg("rtl_file"),
             py::arg("top_module"),
             py::arg("techmap_file") = "")
        .def("is_available",  &SynthEngine::isAvailable,
             "True if yosys is found in the environment")
        .def("get_version",   &SynthEngine::getVersion,
             "Return the Yosys version string")
        .def("get_yosys_path",&SynthEngine::getYosysPath,
             "Return the absolute path to the yosys binary");
}
