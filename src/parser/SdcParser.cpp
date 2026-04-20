#include "parser/SdcParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

// ============================================================
// Public entry point
// ============================================================
bool SdcParser::parse(const std::string& filename, SdcConstraints& out) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[SDC] Cannot open " << filename << std::endl;
        return false;
    }

    std::string line;
    std::string accumulated;  // handle line-continuation backslash

    while (std::getline(file, line)) {
        // Strip comments (#)
        auto commentPos = line.find('#');
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);

        // Trim trailing whitespace
        while (!line.empty() && std::isspace((unsigned char)line.back()))
            line.pop_back();

        // Line continuation: trailing backslash
        if (!line.empty() && line.back() == '\\') {
            line.pop_back();
            accumulated += line + " ";
            continue;
        }

        accumulated += line;
        if (!accumulated.empty())
            parseLine(accumulated, out);
        accumulated.clear();
    }
    if (!accumulated.empty())
        parseLine(accumulated, out);

    std::cout << "[SDC] Loaded " << filename << ": "
              << out.clocks.size()       << " clocks, "
              << out.inputDelays.size()  << " input_delays, "
              << out.outputDelays.size() << " output_delays, "
              << out.falsePaths.size()   << " false_paths, "
              << out.multicyclePaths.size() << " multicycle_paths"
              << std::endl;
    return true;
}

// ============================================================
// Tokenizer — handles embedded [get_ports X] as single tokens
// ============================================================
std::vector<std::string> SdcParser::tokenize(const std::string& line) const {
    std::vector<std::string> tokens;
    std::string cur;
    int bracketDepth = 0;

    for (char ch : line) {
        if (ch == '[') {
            bracketDepth++;
            cur += ch;
        } else if (ch == ']') {
            bracketDepth--;
            cur += ch;
            if (bracketDepth == 0) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else if (bracketDepth > 0) {
            cur += ch;
        } else if (std::isspace((unsigned char)ch)) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// Extract the target name from [get_ports foo], [get_pins foo/bar], [get_clocks clk]
// Returns the last word inside the brackets (after the command keyword)
std::string SdcParser::extractTarget(const std::string& arg) const {
    // arg might be like "[get_ports A]" or "[get_ports {A B}]"
    if (arg.empty() || arg[0] != '[') return arg;

    auto open = arg.find('[');
    auto close = arg.rfind(']');
    if (open == std::string::npos || close == std::string::npos) return arg;

    std::string inner = arg.substr(open + 1, close - open - 1);
    // inner = "get_ports A" or "get_clocks clk"
    std::istringstream ss(inner);
    std::string cmd, target;
    ss >> cmd >> target;

    // Strip braces: {A B} → A (take first)
    if (!target.empty() && target.front() == '{') {
        target = target.substr(1);
        auto brace = target.find('}');
        if (brace != std::string::npos) target = target.substr(0, brace);
        // If multiple, take first word
        auto sp = target.find(' ');
        if (sp != std::string::npos) target = target.substr(0, sp);
    }
    return target;
}

// ============================================================
// Dispatch per command
// ============================================================
void SdcParser::parseLine(const std::string& line, SdcConstraints& out) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return;

    const std::string& cmd = tokens[0];

    if      (cmd == "create_clock")           parseCreateClock(tokens, out);
    else if (cmd == "set_input_delay")        parseSetInputDelay(tokens, out);
    else if (cmd == "set_output_delay")       parseSetOutputDelay(tokens, out);
    else if (cmd == "set_false_path")         parseSetFalsePath(tokens, out);
    else if (cmd == "set_multicycle_path")    parseSetMulticyclePath(tokens, out);
    else if (cmd == "set_clock_uncertainty")  parseSetClockUncertainty(tokens, out);
    else if (cmd == "set_clock_latency")      parseSetClockLatency(tokens, out);
    // Silently ignore: set_load, set_driving_cell, set_max_transition, etc.
}

// ============================================================
// create_clock -period <val> [-name <name>] [get_ports <port>]
// ============================================================
void SdcParser::parseCreateClock(const std::vector<std::string>& tok,
                                  SdcConstraints& out) {
    ClockDef clk;
    clk.period_ps = 1000.0;

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-period" && i + 1 < (int)tok.size()) {
            clk.period_ps = toPs(std::stod(tok[++i]));
        } else if (tok[i] == "-name" && i + 1 < (int)tok.size()) {
            clk.name = tok[++i];
        } else if (tok[i] == "-waveform" && i + 1 < (int)tok.size()) {
            // skip: optional {rise fall} argument
            ++i;
        } else if (tok[i][0] == '[') {
            clk.port = extractTarget(tok[i]);
            if (clk.name.empty()) clk.name = clk.port;
        }
    }
    clk.fall_ps = clk.period_ps / 2.0;
    if (clk.name.empty()) clk.name = "clk";
    out.clocks.push_back(clk);
}

// ============================================================
// set_input_delay -clock <clk> [-max|-min] <val> [get_ports <port>]
// ============================================================
void SdcParser::parseSetInputDelay(const std::vector<std::string>& tok,
                                    SdcConstraints& out) {
    InputDelaySdc d;
    d.is_max = true;  // default: max (setup)

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-clock" && i + 1 < (int)tok.size()) {
            d.clock = extractTarget(tok[++i]);
        } else if (tok[i] == "-max") {
            d.is_max = true;
        } else if (tok[i] == "-min") {
            d.is_max = false;
        } else if (tok[i][0] == '[') {
            d.port = extractTarget(tok[i]);
        } else {
            // Bare numeric value
            try { d.delay_ps = toPs(std::stod(tok[i])); } catch (...) {}
        }
    }
    out.inputDelays.push_back(d);
}

// ============================================================
// set_output_delay -clock <clk> [-max|-min] <val> [get_ports <port>]
// ============================================================
void SdcParser::parseSetOutputDelay(const std::vector<std::string>& tok,
                                     SdcConstraints& out) {
    OutputDelaySdc d;
    d.is_max = true;

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-clock" && i + 1 < (int)tok.size()) {
            d.clock = extractTarget(tok[++i]);
        } else if (tok[i] == "-max") {
            d.is_max = true;
        } else if (tok[i] == "-min") {
            d.is_max = false;
        } else if (tok[i][0] == '[') {
            d.port = extractTarget(tok[i]);
        } else {
            try { d.delay_ps = toPs(std::stod(tok[i])); } catch (...) {}
        }
    }
    out.outputDelays.push_back(d);
}

// ============================================================
// set_false_path [-from <from>] [-to <to>]
// ============================================================
void SdcParser::parseSetFalsePath(const std::vector<std::string>& tok,
                                   SdcConstraints& out) {
    FalsePathSdc fp;
    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-from" && i + 1 < (int)tok.size()) {
            fp.from = extractTarget(tok[++i]);
        } else if (tok[i] == "-to" && i + 1 < (int)tok.size()) {
            fp.to = extractTarget(tok[++i]);
        }
    }
    out.falsePaths.push_back(fp);
}

// ============================================================
// set_multicycle_path <N> [-setup|-hold] [-from <from>] [-to <to>]
// ============================================================
void SdcParser::parseSetMulticyclePath(const std::vector<std::string>& tok,
                                        SdcConstraints& out) {
    MulticyclePathSdc mc;
    mc.multiplier = 1;
    mc.is_setup   = true;

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-setup") {
            mc.is_setup = true;
        } else if (tok[i] == "-hold") {
            mc.is_setup = false;
        } else if (tok[i] == "-from" && i + 1 < (int)tok.size()) {
            mc.from = extractTarget(tok[++i]);
        } else if (tok[i] == "-to" && i + 1 < (int)tok.size()) {
            mc.to = extractTarget(tok[++i]);
        } else {
            try {
                int n = std::stoi(tok[i]);
                if (n > 0) mc.multiplier = n;
            } catch (...) {}
        }
    }
    out.multicyclePaths.push_back(mc);
}

// ============================================================
// set_clock_uncertainty <val> [get_clocks <clk>]
// ============================================================
void SdcParser::parseSetClockUncertainty(const std::vector<std::string>& tok,
                                          SdcConstraints& out) {
    double val = 0.0;
    std::string clkName;

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i][0] == '[') {
            clkName = extractTarget(tok[i]);
        } else {
            try { val = toPs(std::stod(tok[i])); } catch (...) {}
        }
    }

    if (!clkName.empty()) {
        for (auto& c : out.clocks)
            if (c.name == clkName) c.uncertainty_ps = val;
    } else {
        out.globalClockUncertainty_ps = val;
        for (auto& c : out.clocks) c.uncertainty_ps = val;
    }
}

// ============================================================
// set_clock_latency <val> [get_clocks <clk>]
// ============================================================
void SdcParser::parseSetClockLatency(const std::vector<std::string>& tok,
                                      SdcConstraints& out) {
    double val = 0.0;
    std::string clkName;
    bool isSource = false;

    for (int i = 1; i < (int)tok.size(); i++) {
        if (tok[i] == "-source") isSource = true;
        else if (tok[i][0] == '[') clkName = extractTarget(tok[i]);
        else { try { val = toPs(std::stod(tok[i])); } catch (...) {} }
    }
    (void)isSource;  // both source and network latency treated identically

    if (!clkName.empty()) {
        for (auto& c : out.clocks)
            if (c.name == clkName) c.latency_ps = val;
    } else {
        out.globalClockLatency_ps = val;
        for (auto& c : out.clocks) c.latency_ps = val;
    }
}
