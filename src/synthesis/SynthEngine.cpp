#include "synthesis/SynthEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
// Built-in techmap: maps Yosys RTLIL primitives → simple.lib
// ============================================================
static const char* DEFAULT_TECHMAP = R"V(
// Built-in techmap — maps RTLIL gate primitives to simple.lib cells.

module \$_NOT_ (A, Y);
  input A; output Y;
  NOT _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule

module \$_AND_ (A, B, Y);
  input A, B; output Y;
  AND2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_OR_ (A, B, Y);
  input A, B; output Y;
  OR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_NAND_ (A, B, Y);
  input A, B; output Y;
  NAND2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_NOR_ (A, B, Y);
  input A, B; output Y;
  NOR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_XOR_ (A, B, Y);
  input A, B; output Y;
  XOR2 _TECHMAP_REPLACE_ (.A(A), .B(B), .Y(Y));
endmodule

module \$_BUF_ (A, Y);
  input A; output Y;
  BUF _TECHMAP_REPLACE_ (.A(A), .Y(Y));
endmodule

// MUX: Y = S ? B : A  =>  AND/OR/NOT
module \$_MUX_ (A, B, S, Y);
  input A, B, S; output Y;
  wire _sn, _sa, _sb;
  NOT  _ns  (.A(S),   .Y(_sn));
  AND2 _aa  (.A(A),   .B(_sn), .Y(_sa));
  AND2 _ab  (.A(B),   .B(S),   .Y(_sb));
  OR2  _oy  (.A(_sa), .B(_sb), .Y(Y));
endmodule

// Positive-edge DFF (no reset)
module \$_DFF_P_ (C, D, Q);
  input C, D; output Q;
  DFF _TECHMAP_REPLACE_ (.C(C), .D(D), .Q(Q));
endmodule

// Sync-reset DFF (active-high reset → 0): D_gated = D & ~R
module \$_SDFF_PP0_ (C, D, R, Q);
  input C, D, R; output Q;
  wire _rn, _dg;
  NOT  _nr (.A(R),    .Y(_rn));
  AND2 _ad (.A(D),    .B(_rn), .Y(_dg));
  DFF  _df (.C(C),    .D(_dg), .Q(Q));
endmodule

// Async-reset DFF (approximate: modelled as sync)
module \$_DFF_PP0_ (C, D, R, Q);
  input C, D, R; output Q;
  wire _rn, _dg;
  NOT  _nr (.A(R),    .Y(_rn));
  AND2 _ad (.A(D),    .B(_rn), .Y(_dg));
  DFF  _df (.C(C),    .D(_dg), .Q(Q));
endmodule
)V";

// ============================================================
// Helpers
// ============================================================

bool SynthEngine::fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

std::string SynthEngine::readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int SynthEngine::countCells(const std::string& netlistPath) const {
    std::ifstream f(netlistPath);
    if (!f.is_open()) return 0;
    int count = 0;
    std::string line;
    while (std::getline(f, line)) {
        // A cell instantiation looks like:  CELLNAME _identifier_ (
        // Skip module/endmodule/input/output/wire/assign lines.
        std::istringstream ss(line);
        std::string tok;
        if (!(ss >> tok)) continue;
        if (tok == "module" || tok == "endmodule" || tok == "input"  ||
            tok == "output" || tok == "wire"      || tok == "assign" ||
            tok == "//"     || tok == "/*"        || tok.empty()     ||
            tok[0] == '`'   || tok == "reg"       || tok == "always" )
            continue;
        // Cell name cannot start with '$' (unmapped RTLIL cells)
        if (!tok.empty() && tok[0] == '$') continue;
        // Must be followed by an identifier and '(' — check next token
        std::string inst;
        if (ss >> inst && !inst.empty() && inst[0] != '/' && inst != ";")
            count++;
    }
    return count;
}

// ============================================================
// Yosys discovery
// ============================================================

std::string SynthEngine::findYosys() const {
#ifdef _WIN32
    const char* candidates[] = {
        "C:\\oss-cad-suite\\bin\\yosys.exe",
        "C:\\yosys\\bin\\yosys.exe",
        "C:\\Program Files\\yosys\\yosys.exe",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i)
        if (fileExists(candidates[i])) return candidates[i];
    // Also try PATH via where.exe
    {
        FILE* fp = _popen("where yosys 2>NUL", "r");
        if (fp) {
            char buf[512] = {};
            if (fgets(buf, sizeof(buf), fp)) {
                std::string p(buf);
                while (!p.empty() && (p.back()=='\n'||p.back()=='\r'||p.back()==' '))
                    p.pop_back();
                fclose(fp);
                if (!p.empty() && fileExists(p)) return p;
            } else { fclose(fp); }
        }
    }
#else
    const char* candidates[] = {
        "/usr/bin/yosys",
        "/usr/local/bin/yosys",
        "/opt/yosys/bin/yosys",
        "/opt/oss-cad-suite/bin/yosys",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i)
        if (fileExists(candidates[i])) return candidates[i];
    {
        FILE* fp = popen("which yosys 2>/dev/null", "r");
        if (fp) {
            char buf[512] = {};
            if (fgets(buf, sizeof(buf), fp)) {
                std::string p(buf);
                while (!p.empty() && (p.back()=='\n'||p.back()==' ')) p.pop_back();
                pclose(fp);
                if (!p.empty() && fileExists(p)) return p;
            } else { pclose(fp); }
        }
    }
#endif
    return "";
}

// Build the shell command, prepending the yosys lib directory to PATH
// so that required DLLs (libreadline8.dll on Windows) are found.
std::string SynthEngine::buildShellCmd(const std::string& yosysPath,
                                        const std::string& scriptPath,
                                        const std::string& logPath) const {
    // Determine lib dir: parent of bin dir (e.g. C:\oss-cad-suite\bin\..\lib)
    std::string binDir = fs::path(yosysPath).parent_path().string();
    std::string libDir = (fs::path(binDir) / ".." / "lib").string();
    // Normalise
    try { libDir = fs::canonical(fs::path(libDir)).string(); }
    catch (...) {}

#ifdef _WIN32
    // Use cmd /c to set PATH and run yosys, redirect stderr+stdout to log
    std::string cmd =
        "cmd.exe /c \"set \"PATH=" + libDir + ";" + binDir + ";%PATH%\" && "
        "\"" + yosysPath + "\" -q -l \"" + logPath + "\" \"" + scriptPath + "\"\"";
    return cmd;
#else
    return "PATH=\"" + libDir + ":" + binDir + ":$PATH\" \""
         + yosysPath + "\" -q -l \"" + logPath + "\" \"" + scriptPath + "\"";
#endif
}

// ============================================================
// Script writer
// ============================================================
bool SynthEngine::writeScript(const std::string& rtlFile,
                               const std::string& techmapFile,
                               const std::string& topModule,
                               const std::string& outNetlist,
                               const std::string& scriptPath) const {
    std::ofstream f(scriptPath);
    if (!f.is_open()) return false;

    auto q = [](const std::string& s) { return "\"" + s + "\""; };

    f << "read_verilog "  << q(rtlFile) << "\n";
    f << "hierarchy -check -top " << topModule << "\n";
    f << "proc\n";
    f << "opt\n";
    f << "fsm\n";
    f << "opt\n";
    f << "memory\n";
    f << "opt\n";
    f << "techmap\n";
    f << "opt\n";
    f << "techmap -map " << q(techmapFile) << "\n";
    f << "opt\n";
    f << "clean\n";
    f << "write_verilog -noattr " << q(outNetlist) << "\n";
    return true;
}

// ============================================================
// Public API
// ============================================================

bool SynthEngine::isAvailable() const { return !findYosys().empty(); }

std::string SynthEngine::getYosysPath() const { return findYosys(); }

std::string SynthEngine::getVersion() const {
    std::string yosys = findYosys();
    if (yosys.empty()) return "";

    std::string libDir;
    try {
        std::string binDir = fs::path(yosys).parent_path().string();
        libDir = fs::canonical(fs::path(binDir) / ".." / "lib").string();
    } catch (...) {}

#ifdef _WIN32
    std::string cmd = "cmd.exe /c \"set \"PATH=" + libDir + ";%PATH%\" && \""
                    + yosys + "\" --version 2>&1\"";
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    std::string cmd = "PATH=\"" + libDir + ":$PATH\" \"" + yosys
                    + "\" --version 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
#endif
    if (!fp) return "";
    char buf[256] = {};
    fgets(buf, sizeof(buf), fp);
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
    std::string ver(buf);
    while (!ver.empty() && (ver.back()=='\n'||ver.back()=='\r')) ver.pop_back();
    return ver;
}

SynthResult SynthEngine::synthesize(const std::string& rtlFile,
                                     const std::string& topModule,
                                     const std::string& techmapFileIn) {
    SynthResult result;

    // 1. Find yosys
    std::string yosys = findYosys();
    if (yosys.empty()) {
        result.errorMessage = "yosys not found. Install oss-cad-suite or add yosys to PATH.";
        std::cerr << "  [Synth] " << result.errorMessage << "\n";
        return result;
    }

    if (!fileExists(rtlFile)) {
        result.errorMessage = "RTL file not found: " + rtlFile;
        std::cerr << "  [Synth] " << result.errorMessage << "\n";
        return result;
    }

    // 2. Determine work paths (sibling files next to the input)
    fs::path rtlPath(rtlFile);
    std::string stem = rtlPath.stem().string();
    fs::path workDir = rtlPath.parent_path();

    std::string outNetlist = (workDir / (stem + "_synthesized.v")).string();
    std::string scriptPath = (workDir / (stem + "_synth.ys")).string();
    std::string logPath    = (workDir / (stem + "_synth.log")).string();
    std::string tmapPath   = (workDir / (stem + "_techmap.v")).string();

    // 3. Resolve techmap: use caller's file or write built-in
    std::string techmapFile = techmapFileIn;
    if (techmapFile.empty() || !fileExists(techmapFile)) {
        std::ofstream tmf(tmapPath);
        if (!tmf.is_open()) {
            result.errorMessage = "Cannot write techmap to: " + tmapPath;
            return result;
        }
        tmf << DEFAULT_TECHMAP;
        tmf.close();
        techmapFile = tmapPath;
    }

    // 4. Write Yosys script
    if (!writeScript(rtlFile, techmapFile, topModule, outNetlist, scriptPath)) {
        result.errorMessage = "Cannot write synthesis script to: " + scriptPath;
        return result;
    }

    std::cout << "  [Synth] Running Yosys on " << rtlFile
              << " (top=" << topModule << ")...\n";

    // 5. Execute
    std::string cmd = buildShellCmd(yosys, scriptPath, logPath);
    int rc = std::system(cmd.c_str());
    result.log = readFile(logPath);

    // 6. Clean up temp files
    try { fs::remove(scriptPath); } catch (...) {}
    try { if (techmapFileIn.empty()) fs::remove(tmapPath); } catch (...) {}
    try { fs::remove(logPath); } catch (...) {}

    // 7. Verify output
    if (rc != 0 || !fileExists(outNetlist)) {
        result.errorMessage = "Yosys failed (exit=" + std::to_string(rc)
                            + "). Log:\n" + result.log;
        std::cerr << "  [Synth] Failed — " << result.errorMessage << "\n";
        return result;
    }

    result.success      = true;
    result.outputNetlist = outNetlist;
    result.cellCount    = countCells(outNetlist);

    std::cout << "  [Synth] Done — " << result.cellCount
              << " cells in " << outNetlist << "\n";
    return result;
}
