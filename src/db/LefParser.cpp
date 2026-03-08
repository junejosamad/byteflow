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
    CellDef* currentMacro = nullptr;
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
            if (design->cellLibrary) {
                currentMacro = design->cellLibrary->getCell(macroName);
                if (!currentMacro) {
                    currentMacro = new CellDef();
                    currentMacro->name = macroName;
                    design->cellLibrary->addCell(currentMacro);
                }
                // we'll determine if it's a true macro by its size
            }
            
        } else if (token == "SIZE" && currentMacro) {
            double w, h;
            std::string by;
            iss >> w >> by >> h;
            currentMacro->width = w;
            currentMacro->height = h;
            
            // Standard cells are small (e.g. 1x1, 4x2). Real SoC Macros (SRAM) are large.
            if (w > 10.0 || h > 10.0) {
                currentMacro->isMacro = true;
            } else {
                currentMacro->isMacro = false;
            }
            
        } else if (token == "PIN" && currentMacro) {
            iss >> currentPinName;
            
            // Check if pin exists
            bool found = false;
            for (auto& p : currentMacro->pins) {
                if (p.name == currentPinName) { found = true; break; }
            }
            if (!found) {
                PinDef pDef;
                pDef.name = currentPinName;
                currentMacro->pins.push_back(pDef);
            }
            
        } else if (token == "DIRECTION" && currentPinName != "") {
            std::string dir;
            iss >> dir;
            for (auto& p : currentMacro->pins) {
                if (p.name == currentPinName) {
                    p.isOutput = (dir == "OUTPUT");
                    break;
                }
            }
            
        } else if (token == "RECT" && currentPinName != "") {
            double x1, y1, x2, y2;
            iss >> x1 >> y1 >> x2 >> y2;
            
            // Calculate the exact center offset of the pin's metal rectangle
            for (auto& p : currentMacro->pins) {
                if (p.name == currentPinName) {
                    p.dx = (x1 + x2) / 2.0;
                    p.dy = (y1 + y2) / 2.0;
                    break;
                }
            }
            
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
    std::cout << "  [LEF] Library loaded successfully. Parsed geometry for macros/cells.\n";
}
