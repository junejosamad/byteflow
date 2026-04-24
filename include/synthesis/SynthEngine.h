#pragma once
#include <string>

// ============================================================
// Synthesis Result
// ============================================================
struct SynthResult {
    bool        success       = false;
    std::string outputNetlist;   // absolute path to synthesized structural Verilog
    int         cellCount     = 0;
    std::string log;             // full Yosys log output
    std::string errorMessage;    // set on failure
};

// ============================================================
// Synthesis Engine
//
// Wraps Yosys to convert behavioral/RTL Verilog into structural
// Verilog using a technology-specific cell library.
//
// The synthesis flow:
//   read_verilog → hierarchy → proc/opt/fsm/memory → techmap →
//   techmap -map <cell_techmap> → opt/clean → write_verilog
//
// Technology mapping uses a Verilog techmap file (not ABC), so
// the flow works without the ABC binary present.
// ============================================================
class SynthEngine {
public:
    // Synthesize RTL Verilog to structural netlist.
    //   rtlFile    : path to input behavioral/RTL Verilog
    //   topModule  : top-level module name
    //   techmapFile: optional path to a Yosys techmap .v file;
    //                if empty, the built-in simple.lib techmap is used
    SynthResult synthesize(const std::string& rtlFile,
                           const std::string& topModule,
                           const std::string& techmapFile = "");

    // Returns true when yosys is reachable in the environment.
    bool        isAvailable()  const;

    // Returns the Yosys version string, empty if not found.
    std::string getVersion()   const;

    // Returns the absolute path to the yosys binary, or empty.
    std::string getYosysPath() const;

private:
    // Locate the yosys binary (searches PATH + common install dirs).
    std::string findYosys()    const;

    // Build OS-appropriate shell command to run Yosys.
    std::string buildShellCmd(const std::string& yosysPath,
                              const std::string& scriptPath,
                              const std::string& logPath) const;

    // Write the .ys synthesis script to disk.
    bool writeScript(const std::string& rtlFile,
                     const std::string& techmapFile,
                     const std::string& topModule,
                     const std::string& outNetlist,
                     const std::string& scriptPath) const;

    // Count cell instantiations in the output Verilog.
    int countCells(const std::string& netlistPath) const;

    // Read a text file into a string.
    static std::string readFile(const std::string& path);

    // True if a regular file exists and is non-empty.
    static bool fileExists(const std::string& path);
};
