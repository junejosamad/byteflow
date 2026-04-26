#pragma once
#include "db/Design.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

// ============================================================
// TclEngine — lightweight built-in EDA script interpreter
//
// Supports a TCL-compatible command syntax for driving the full
// OpenEDA flow from .tcl batch scripts or Python via pybind11.
//
// Supported commands:
//   puts <msg>
//   set <var> <value>
//   read_verilog <file>
//   read_liberty  <file>
//   read_lef      <file>
//   read_sdc      <file>
//   place_design
//   route_design
//   write_gds     <file>
//   write_spef    <file>
//   report_timing [-period <ps>]
//   check_drc
//   check_lvs
//   help
// ============================================================
class TclEngine {
public:
    explicit TclEngine(Design* design);

    // Execute a .tcl script file. Returns true if all commands succeeded.
    bool runScript(const std::string& filename);

    // Execute a single command string. Returns true on success.
    bool runCommand(const std::string& cmd);

    // Accumulated output from puts / report commands since last clearOutput()
    std::string getOutput() const { return output_; }

    // Last error message (empty if no error)
    std::string getError()  const { return error_;  }

    void clearOutput() { output_.clear(); error_.clear(); }

private:
    Design* design_;
    std::map<std::string, std::string> vars_;
    std::string output_;
    std::string error_;

    // Command handlers — each returns true on success
    bool cmdPuts        (const std::vector<std::string>& args);
    bool cmdSet         (const std::vector<std::string>& args);
    bool cmdReadVerilog (const std::vector<std::string>& args);
    bool cmdReadLiberty (const std::vector<std::string>& args);
    bool cmdReadLef     (const std::vector<std::string>& args);
    bool cmdReadSdc     (const std::vector<std::string>& args);
    bool cmdPlaceDesign (const std::vector<std::string>& args);
    bool cmdRouteDesign (const std::vector<std::string>& args);
    bool cmdWriteGds    (const std::vector<std::string>& args);
    bool cmdWriteSpef   (const std::vector<std::string>& args);
    bool cmdReportTiming(const std::vector<std::string>& args);
    bool cmdCheckDrc    (const std::vector<std::string>& args);
    bool cmdCheckLvs    (const std::vector<std::string>& args);
    bool cmdHelp        (const std::vector<std::string>& args);

    // Parser helpers
    std::vector<std::string> tokenize(const std::string& line) const;
    std::string substituteVars(const std::string& s) const;
    bool execTokens(const std::vector<std::string>& toks);

    void emit(const std::string& msg);  // append to output_
    bool fail(const std::string& msg);  // set error_, return false
};
