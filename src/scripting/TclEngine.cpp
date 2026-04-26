#include "scripting/TclEngine.h"
#include "db/LibertyParser.h"
#include "db/LefParser.h"
#include "parser/VerilogParser.h"
#include "parser/SdcParser.h"
#include "place/PlaceEngine.h"
#include "place/Legalizer.h"
#include "route/RouteEngine.h"
#include "timer/Timer.h"
#include "analysis/SpefEngine.h"
#include "analysis/DrcEngine.h"
#include "analysis/LvsEngine.h"
#include "export/GdsExporter.h"
#include "util/Logger.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// ============================================================
// Constructor
// ============================================================
TclEngine::TclEngine(Design* design) : design_(design) {}

// ============================================================
// Output helpers
// ============================================================
void TclEngine::emit(const std::string& msg) {
    output_ += msg + "\n";
    std::cout << msg << "\n";
}

bool TclEngine::fail(const std::string& msg) {
    error_ = msg;
    std::cout << "[TclEngine] ERROR: " << msg << "\n";
    return false;
}

// ============================================================
// Tokenizer — splits on whitespace, handles quoted strings
// ============================================================
std::vector<std::string> TclEngine::tokenize(const std::string& line) const {
    std::vector<std::string> toks;
    std::string cur;
    bool inQuote = false;
    for (char c : line) {
        if (c == '"') { inQuote = !inQuote; continue; }
        if (inQuote) { cur += c; continue; }
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

// ============================================================
// Variable substitution: $var or ${var}
// ============================================================
std::string TclEngine::substituteVars(const std::string& s) const {
    std::string result;
    result.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '$' && i + 1 < s.size()) {
            ++i;
            bool braced = (s[i] == '{');
            if (braced) ++i;
            std::string varName;
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_'))
                varName += s[i++];
            if (braced && i < s.size() && s[i] == '}') ++i;
            auto it = vars_.find(varName);
            result += (it != vars_.end()) ? it->second : ("$" + varName);
        } else {
            result += s[i++];
        }
    }
    return result;
}

// ============================================================
// Script execution
// ============================================================
bool TclEngine::runScript(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.is_open())
        return fail("cannot open script: " + filename);

    emit("[TclEngine] Running script: " + filename);
    int lineNum = 0;
    bool ok = true;
    std::string line;
    while (std::getline(f, line)) {
        ++lineNum;
        // trim
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        // skip comments
        if (line[0] == '#') continue;
        if (!runCommand(line)) {
            fail("script error at line " + std::to_string(lineNum) + ": " + error_);
            ok = false;
            // continue executing rest of script (don't abort on first error)
        }
    }
    return ok;
}

bool TclEngine::runCommand(const std::string& cmd) {
    error_.clear();
    // trim + substitute vars
    size_t s = cmd.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return true;
    std::string trimmed = cmd.substr(s);
    if (trimmed.empty() || trimmed[0] == '#') return true;
    std::string expanded = substituteVars(trimmed);
    auto toks = tokenize(expanded);
    if (toks.empty()) return true;
    return execTokens(toks);
}

bool TclEngine::execTokens(const std::vector<std::string>& toks) {
    const std::string& cmd = toks[0];
    std::vector<std::string> args(toks.begin() + 1, toks.end());

    if (cmd == "puts")          return cmdPuts(args);
    if (cmd == "set")           return cmdSet(args);
    if (cmd == "read_verilog")  return cmdReadVerilog(args);
    if (cmd == "read_liberty")  return cmdReadLiberty(args);
    if (cmd == "read_lef")      return cmdReadLef(args);
    if (cmd == "read_sdc")      return cmdReadSdc(args);
    if (cmd == "place_design")  return cmdPlaceDesign(args);
    if (cmd == "route_design")  return cmdRouteDesign(args);
    if (cmd == "write_gds")     return cmdWriteGds(args);
    if (cmd == "write_spef")    return cmdWriteSpef(args);
    if (cmd == "report_timing") return cmdReportTiming(args);
    if (cmd == "check_drc")     return cmdCheckDrc(args);
    if (cmd == "check_lvs")     return cmdCheckLvs(args);
    if (cmd == "help")          return cmdHelp(args);

    return fail("unknown command: " + cmd + " (type 'help' for available commands)");
}

// ============================================================
// Command implementations
// ============================================================

bool TclEngine::cmdPuts(const std::vector<std::string>& args) {
    std::string msg;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) msg += ' ';
        msg += args[i];
    }
    emit(msg);
    return true;
}

bool TclEngine::cmdSet(const std::vector<std::string>& args) {
    if (args.size() < 2)
        return fail("set: usage: set <variable> <value>");
    vars_[args[0]] = args[1];
    return true;
}

bool TclEngine::cmdReadVerilog(const std::vector<std::string>& args) {
    if (args.empty()) return fail("read_verilog: missing filename");
    if (!design_)     return fail("read_verilog: no design context");
    if (!design_->cellLibrary || design_->cellLibrary->cells.empty())
        return fail("read_verilog: call read_liberty first");
    try {
        VerilogParser parser;
        bool ok = parser.read(args[0], *design_, *design_->cellLibrary);
        if (!ok) return fail("read_verilog: parse failed for " + args[0]);
        double coreSize = std::max(400.0,
            std::sqrt((double)design_->instances.size()) * 30.0);
        design_->coreWidth  = coreSize;
        design_->coreHeight = coreSize;
        emit("[TCL] read_verilog: loaded " + args[0] +
             " (" + std::to_string(design_->instances.size()) + " instances)");
        return true;
    } catch (const std::exception& e) {
        return fail("read_verilog: " + std::string(e.what()));
    }
}

bool TclEngine::cmdReadLiberty(const std::vector<std::string>& args) {
    if (args.empty()) return fail("read_liberty: missing filename");
    if (!design_)     return fail("read_liberty: no design context");
    Library* lib = new Library();
    design_->cellLibrary = lib;
    LibertyParser parser;
    parser.parse(args[0], *lib);
    if (lib->cells.empty())
        return fail("read_liberty: no cells parsed from " + args[0]);
    emit("[TCL] read_liberty: loaded " + std::to_string(lib->cells.size()) +
         " cells from " + args[0]);
    return true;
}

bool TclEngine::cmdReadLef(const std::vector<std::string>& args) {
    if (args.empty()) return fail("read_lef: missing filename");
    if (!design_)     return fail("read_lef: no design context");
    LefParser parser;
    parser.parse(args[0], design_);
    emit("[TCL] read_lef: loaded " + args[0]);
    return true;
}

bool TclEngine::cmdReadSdc(const std::vector<std::string>& args) {
    if (args.empty()) return fail("read_sdc: missing filename");
    if (!design_)     return fail("read_sdc: no design context");
    SdcParser parser;
    bool ok = parser.parse(args[0], design_->sdc);
    if (!ok) return fail("read_sdc: parse failed for " + args[0]);
    emit("[TCL] read_sdc: loaded " + args[0]);
    return true;
}

bool TclEngine::cmdPlaceDesign(const std::vector<std::string>& args) {
    if (!design_) return fail("place_design: no design context");
    double coreW = design_->coreWidth  > 0 ? design_->coreWidth  : 400.0;
    double coreH = design_->coreHeight > 0 ? design_->coreHeight : 400.0;
    Timer physTimer(design_, design_->cellLibrary, nullptr);
    physTimer.buildGraph();
    PlaceEngine placer(design_, &physTimer);
    placer.runPlacement(*design_, coreW, coreH);
    Legalizer leg(design_, coreW, coreH);
    leg.run();
    for (GateInstance* inst : design_->instances) inst->isPlaced = true;
    emit("[TCL] place_design: placed " +
         std::to_string(design_->instances.size()) + " instances");
    return true;
}

bool TclEngine::cmdRouteDesign(const std::vector<std::string>& args) {
    if (!design_) return fail("route_design: no design context");
    RouteEngine router;
    int cw = static_cast<int>(design_->coreWidth  > 0 ? design_->coreWidth  : 400);
    int ch = static_cast<int>(design_->coreHeight > 0 ? design_->coreHeight : 400);
    router.runRouting(*design_, cw, ch);
    emit("[TCL] route_design: routed " +
         std::to_string(design_->nets.size()) + " nets");
    return true;
}

bool TclEngine::cmdWriteGds(const std::vector<std::string>& args) {
    if (args.empty()) return fail("write_gds: missing filename");
    if (!design_)     return fail("write_gds: no design context");
    GdsExporter::exportGds(args[0], design_);
    emit("[TCL] write_gds: exported " + args[0]);
    return true;
}

bool TclEngine::cmdWriteSpef(const std::vector<std::string>& args) {
    if (args.empty()) return fail("write_spef: missing filename");
    if (!design_)     return fail("write_spef: no design context");
    SpefEngine spef;
    spef.extract(*design_);
    spef.writeSpef(args[0], *design_);
    emit("[TCL] write_spef: exported " + args[0]);
    return true;
}

bool TclEngine::cmdReportTiming(const std::vector<std::string>& args) {
    if (!design_) return fail("report_timing: no design context");
    double period = 1000.0; // default 1 GHz
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-period") {
            try { period = std::stod(args[i + 1]); } catch (...) {}
        }
    }
    SpefEngine spef;
    spef.extract(*design_);
    Timer timer(design_, design_->cellLibrary, &spef);
    timer.buildGraph();
    timer.setClockPeriod(period);
    timer.updateTiming();
    auto sum = timer.getSummary();
    std::string rpt =
        "[TCL] report_timing: period=" + std::to_string((int)period) + "ps"
        "  WNS=" + std::to_string(sum.wns) + "ps"
        "  TNS=" + std::to_string(sum.tns) + "ps"
        "  Viols=" + std::to_string(sum.violations);
    emit(rpt);
    return true;
}

bool TclEngine::cmdCheckDrc(const std::vector<std::string>& args) {
    if (!design_) return fail("check_drc: no design context");
    DrcEngine drc;
    DrcReport rpt = drc.runDrc(design_);
    emit("[TCL] check_drc: shorts=" + std::to_string(rpt.shortCount()) +
         "  spacing_viols=" + std::to_string(rpt.spacingCount()) +
         "  total=" + std::to_string(rpt.totalCount()));
    return true;
}

bool TclEngine::cmdCheckLvs(const std::vector<std::string>& args) {
    if (!design_) return fail("check_lvs: no design context");
    LvsEngine lvs;
    LvsReport rpt = lvs.runLvs(design_);
    emit("[TCL] check_lvs: mismatches=" + std::to_string(rpt.totalCount()) +
         "  clean=" + std::string(rpt.clean() ? "yes" : "no"));
    return true;
}

bool TclEngine::cmdHelp(const std::vector<std::string>&) {
    emit("Available commands:");
    emit("  puts <msg>                     -- print a message");
    emit("  set <var> <value>              -- set a variable");
    emit("  read_verilog <file>            -- load structural Verilog netlist");
    emit("  read_liberty  <file>           -- load Liberty timing library");
    emit("  read_lef      <file>           -- load LEF physical library");
    emit("  read_sdc      <file>           -- load SDC timing constraints");
    emit("  place_design                   -- run placement + legalization");
    emit("  route_design                   -- run detailed routing");
    emit("  write_gds     <file>           -- export GDSII layout");
    emit("  write_spef    <file>           -- extract + export SPEF parasitics");
    emit("  report_timing [-period <ps>]   -- run STA and print WNS/TNS");
    emit("  check_drc                      -- run DRC and report violations");
    emit("  check_lvs                      -- run LVS and report mismatches");
    emit("  help                           -- show this message");
    return true;
}
