#include "db/LefParser.h"
#include <fstream>
#include <sstream>
#include <iostream>

void LefParser::parse(const std::string& filename, Design* design) {
    std::cout << "  [LEF] Loading physical library " << filename << "...\n";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "  Error: Could not open LEF file.\n";
        return;
    }

    std::string line;
    Macro* currentMacro = nullptr;
    std::string currentPinName = "";

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        
        // Skip empty lines
        if (!(iss >> token)) continue;

        if (token == "MACRO") {
            std::string macroName;
            iss >> macroName;
            
            // If Liberty parser hasn't created it yet, create it. Otherwise, fetch it.
            if (design->library.find(macroName) == design->library.end()) {
                design->library[macroName] = new Macro(macroName);
            }
            currentMacro = design->library[macroName];
            
        } else if (token == "SIZE" && currentMacro) {
            double w, h;
            std::string by;
            iss >> w >> by >> h;
            currentMacro->width = w;
            currentMacro->height = h;
            
        } else if (token == "PIN" && currentMacro) {
            iss >> currentPinName;
            currentMacro->pins[currentPinName].name = currentPinName;
            
        } else if (token == "DIRECTION" && currentPinName != "") {
            std::string dir;
            iss >> dir;
            if (dir == "INPUT") currentMacro->pins[currentPinName].dir = PinType::INPUT;
            else currentMacro->pins[currentPinName].dir = PinType::OUTPUT;
            
        } else if (token == "RECT" && currentPinName != "") {
            double x1, y1, x2, y2;
            iss >> x1 >> y1 >> x2 >> y2;
            
            // Calculate the exact center offset of the pin's metal rectangle
            currentMacro->pins[currentPinName].offsetX = (x1 + x2) / 2.0;
            currentMacro->pins[currentPinName].offsetY = (y1 + y2) / 2.0;
            
        } else if (token == "END") {
            std::string endName;
            iss >> endName;
            if (currentMacro && endName == currentMacro->name) {
                currentMacro = nullptr; // Exit Macro block
            } else if (endName == currentPinName) {
                currentPinName = "";    // Exit Pin block
            }
        }
    }
    std::cout << "  [LEF] Library loaded successfully. Parsed geometry for " << design->library.size() << " macros.\n";
}
