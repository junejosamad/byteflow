#include "db/LibertyParser.h"
#include <fstream>
#include <sstream>
#include <iostream>

void LibertyParser::parse(std::string filename, Library& lib) {
    std::cout << "  [Liberty] Loading library " << filename << "...\n";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open .lib file!\n";
        return;
    }

    std::string line, token;
    CellDef* currentCell = nullptr;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        ss >> token;

        // 1. Found a cell definition: cell(AND2)
        if (token.find("cell(") != std::string::npos) {
            std::string cellName = token.substr(5, token.length() - 6);
            
            // Look up this cell in our existing Library
            // (Assuming Library has a map<string, CellDef*> or similar)
            currentCell = lib.getCell(cellName); 
            
            if (!currentCell) {
                // Auto-create missing cells (e.g., NAND2, NOR2 from simple.lib)
                currentCell = new CellDef();
                currentCell->name = cellName;
                currentCell->width = 1.0; 
                currentCell->height = 1.0; 
                lib.addCell(currentCell);
                // std::cout << "    [Liberty] Auto-created cell: " << cellName << "\n";
            }
        } 
        // 2. Found delay data
        else if (token == "intrinsic_rise" && currentCell) {
            std::string colon, valueStr;
            ss >> colon >> valueStr;
            if (valueStr.back() == ';') valueStr.pop_back();
            try { currentCell->intrinsicDelay = std::stod(valueStr); } catch (...) {}
        }
        // 3. Found Area
        else if (token == "area" && currentCell) {
            std::string colon, valueStr;
            ss >> colon >> valueStr;
            if (valueStr.back() == ';') valueStr.pop_back();
            try {
                double area = std::stod(valueStr);
                // Assume square for now, or fixed height
                currentCell->width = std::sqrt(area);
                currentCell->height = std::sqrt(area);
            } catch(...) {}
        }
        // 4. Found Pin Definition: pin(A) { ... }
        else if (token.find("pin(") != std::string::npos && currentCell) {
            std::string pinName = token.substr(4, token.length() - 5);
            // Check if pin already exists
            bool exists = false;
            for(auto& p : currentCell->pins) if(p.name == pinName) exists = true;
            
            if (!exists) {
                // Parse direction inside the block
                bool isOutput = false;
                std::string subLine;
                while(std::getline(file, subLine)) {
                    if (subLine.find("}") != std::string::npos) break; // End of pin group
                    if (subLine.find("direction") != std::string::npos && subLine.find("output") != std::string::npos) {
                        isOutput = true;
                    }
                }
                // Add Pin to CellDef with default offsets
                // Inputs on Left (-width/2), Outputs on Right (+width/2)
                int numInputs = 0, numOutputs = 0;
                for (auto& p : currentCell->pins) {
                    if (p.isOutput) numOutputs++;
                    else numInputs++;
                }
                double dx = isOutput ? (currentCell->width / 2.0) : -(currentCell->width / 2.0);
                double dy = isOutput ? (2.0 + numOutputs * 2.0) : (2.0 + numInputs * 2.0);
                currentCell->pins.push_back({ pinName, isOutput, 0.0, dx, dy });
                // std::cout << "    [Liberty] Added Pin: " << pinName << " (" << (isOutput ? "OUT" : "IN") << ")\n";
            }
        }
    }
    std::cout << "  [Liberty] Library loaded successfully.\n";
}
