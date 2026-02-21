#include "parser/VerilogParser.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

std::string VerilogParser::clean(std::string s) {
    // Remove spaces and commas (e.g., ".A( n1 )," -> ".A(n1)")
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
    s.erase(remove(s.begin(), s.end(), ','), s.end());
    return s;
}

bool VerilogParser::read(std::string filename, Design& design, Library& lib) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << "\n";
        return false;
    }

    std::cout << "Parsing " << filename << "...\n";
    
    // 1. Read entire file into string
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    // 2. Remove newlines/carriage returns (Treat as one long line)
    for (char &c : content) {
        if (c == '\n' || c == '\r') c = ' ';
    }

    // 3. Split by Semicolon ';'
    std::stringstream ss(content);
    std::string segment;
    
    // Updates Regexes (Removed trailing ;)
    std::regex moduleRegex(R"(module\s+(\w+))");
    std::regex gateRegex(R"((\w+)\s+(\w+)\s*\((.*)\))"); // No semicolon at end
    std::regex pinRegex(R"(\.(\w+)\(([^)]+)\))");

    while (std::getline(ss, segment, ';')) {
        std::smatch match;

        // CHECK 1: Is this the Module Name?
        if (std::regex_search(segment, match, moduleRegex)) {
            design.name = match[1];
            std::cout << "  Found Module: " << design.name << "\n";
        }

        // CHECK 2: Is this a Gate? "NAND2 u1 (...)"
        else if (std::regex_search(segment, match, gateRegex)) {
            std::string type = match[1]; 
            std::string name = match[2]; 
            std::string args = match[3]; 

            // Filter out keywords like "wire", "input", "output", "assign"
            if (type == "wire" || type == "input" || type == "output" || type == "assign") continue;

            // Does this gate exist in our Library?
            CellDef* cellDef = lib.getCell(type);
            if (!cellDef) {
                // std::cerr << "  Warning: Unknown Cell Type '" << type << "'\n";
                continue;
            }

            // Create the GateInstance
            GateInstance* inst = new GateInstance(name, cellDef);
            design.addInstance(inst);

            // Parse Pins
            auto args_begin = std::sregex_iterator(args.begin(), args.end(), pinRegex);
            auto args_end = std::sregex_iterator();

            for (std::sregex_iterator i = args_begin; i != args_end; ++i) {
                std::smatch pinMatch = *i;
                std::string pinName = pinMatch[1];
                std::string netName = clean(pinMatch[2]);
                design.connect(inst, pinName, netName);
            }
        }
    }

    file.close();
    return true;
}
