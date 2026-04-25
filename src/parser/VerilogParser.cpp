#include "parser/VerilogParser.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <cctype>

std::string VerilogParser::clean(std::string s) {
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
    s.erase(remove(s.begin(), s.end(), ','), s.end());
    return s;
}

// Manual gate-instantiation parser — avoids std::regex (.*)  which triggers
// catastrophic backtracking on GCC's libstdc++ for certain inputs.
// Returns true and fills type/name/args if the segment is a gate instantiation.
static bool parseGate(const std::string& seg,
                      std::string& type, std::string& name, std::string& args)
{
    // Find the first '(' — start of port list
    size_t open = seg.find('(');
    if (open == std::string::npos || open == 0) return false;

    // Find the matching ')' by counting nesting depth
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < seg.size(); ++i) {
        if      (seg[i] == '(') ++depth;
        else if (seg[i] == ')') { if (--depth == 0) { close = i; break; } }
    }
    if (close == std::string::npos) return false;

    args = seg.substr(open + 1, close - open - 1);

    // Extract the prefix before '(' and trim trailing whitespace
    size_t prefixEnd = open;
    while (prefixEnd > 0 && std::isspace((unsigned char)seg[prefixEnd - 1]))
        --prefixEnd;
    if (prefixEnd == 0) return false;

    // Extract instance name (rightmost identifier word in prefix)
    size_t nEnd = prefixEnd;
    size_t nStart = nEnd;
    while (nStart > 0 &&
           (std::isalnum((unsigned char)seg[nStart - 1]) || seg[nStart - 1] == '_'))
        --nStart;
    if (nStart == nEnd) return false;
    name = seg.substr(nStart, nEnd - nStart);

    // Extract cell type (identifier word immediately before instance name)
    size_t tEnd = nStart;
    while (tEnd > 0 && std::isspace((unsigned char)seg[tEnd - 1])) --tEnd;
    if (tEnd == 0) return false;
    size_t tStart = tEnd;
    while (tStart > 0 &&
           (std::isalnum((unsigned char)seg[tStart - 1]) || seg[tStart - 1] == '_'))
        --tStart;
    if (tStart == tEnd) return false;
    type = seg.substr(tStart, tEnd - tStart);

    return true;
}

bool VerilogParser::read(std::string filename, Design& design, Library& lib) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << "\n";
        return false;
    }

    std::cout << "Parsing " << filename << "...\n";

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    for (char& c : content)
        if (c == '\n' || c == '\r') c = ' ';

    std::stringstream ss(content);
    std::string segment;

    // Simple patterns — safe on all std::regex implementations
    std::regex moduleRegex(R"(module\s+(\w+))");
    std::regex pinRegex(R"(\.(\w+)\(([^)]+)\))");

    while (std::getline(ss, segment, ';')) {
        std::smatch match;

        // CHECK 1: Module declaration
        if (std::regex_search(segment, match, moduleRegex)) {
            design.name = match[1];
            std::cout << "  Found Module: " << design.name << "\n";
            std::regex outPortRegex(R"(\boutput\b\s+(?:\[[^\]]*\]\s+)?(\w+))");
            auto it  = std::sregex_iterator(segment.begin(), segment.end(), outPortRegex);
            auto end = std::sregex_iterator();
            for (; it != end; ++it)
                design.primaryOutputNets.insert((*it)[1]);
            continue;
        }

        // CHECK 2: Gate instantiation — manual parse avoids regex backtracking
        std::string type, name, args;
        if (!parseGate(segment, type, name, args)) continue;

        if (type == "output") { design.primaryOutputNets.insert(name); continue; }
        if (type == "wire" || type == "input" || type == "assign" ||
            type == "endmodule" || type == "reg") continue;

        CellDef* cellDef = lib.getCell(type);
        if (!cellDef) continue;

        GateInstance* inst = new GateInstance(name, cellDef);
        design.addInstance(inst);

        auto args_begin = std::sregex_iterator(args.begin(), args.end(), pinRegex);
        auto args_end   = std::sregex_iterator();
        for (std::sregex_iterator i = args_begin; i != args_end; ++i) {
            std::smatch pinMatch = *i;
            design.connect(inst, pinMatch[1], clean(pinMatch[2]));
        }
    }

    file.close();
    return true;
}
