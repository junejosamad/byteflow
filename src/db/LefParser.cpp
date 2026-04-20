#include "db/LefParser.h"
#include <fstream>
#include <sstream>
#include <iostream>

void LefParser::parse(const std::string& filename, Design* design) {
    std::cout << "  [LEF] Loading physical library " << filename << "...\n";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "  [LEF] Error: Could not open " << filename << "\n";
        return;
    }

    std::string line;
    CellDef*    currentMacro   = nullptr;
    std::string currentPinName = "";
    bool        skipPin        = false; // true for POWER/GROUND pins
    double      dbUnits        = 1.0;  // DATABASE MICRONS scale

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        if (!(iss >> token)) continue;

        // Comments
        if (token[0] == '#') continue;

        // ── Units ─────────────────────────────────────────────────────────────
        if (token == "DATABASE") {
            std::string sub;
            iss >> sub; // "MICRONS"
            double val = 1.0;
            if (iss >> val) dbUnits = val;
            continue;
        }

        // ── Macro (cell) definition ───────────────────────────────────────────
        if (token == "MACRO") {
            std::string macroName;
            iss >> macroName;
            if (!design->cellLibrary) continue;
            currentMacro = design->cellLibrary->getCell(macroName);
            if (!currentMacro) {
                currentMacro = new CellDef();
                currentMacro->name = macroName;
                design->cellLibrary->addCell(currentMacro);
            }
            currentPinName = "";
            skipPin        = false;
            continue;
        }

        if (!currentMacro) continue;

        // ── Size ──────────────────────────────────────────────────────────────
        if (token == "SIZE") {
            double w, h;
            std::string by;
            if (iss >> w >> by >> h) {
                currentMacro->width  = w;
                currentMacro->height = h;
                // Sky130 standard cells are <= 2.72µm tall; macros are much larger
                currentMacro->isMacro = (w > 10.0 || h > 10.0);
            }
            continue;
        }

        // ── Pin definition ────────────────────────────────────────────────────
        if (token == "PIN") {
            iss >> currentPinName;
            skipPin = false;
            // Don't add pin to cell yet; wait for USE directive to filter PG pins
            bool found = false;
            for (auto& p : currentMacro->pins)
                if (p.name == currentPinName) { found = true; break; }
            if (!found) {
                PinDef pDef;
                pDef.name = currentPinName;
                currentMacro->pins.push_back(pDef);
            }
            continue;
        }

        // ── Use (SIGNAL / POWER / GROUND) ─────────────────────────────────────
        if (token == "USE" && !currentPinName.empty()) {
            std::string useType;
            iss >> useType;
            if (useType == "POWER" || useType == "GROUND") {
                // Remove this pin from the cell — it's a power/ground rail, not signal
                skipPin = true;
                auto& pins = currentMacro->pins;
                pins.erase(std::remove_if(pins.begin(), pins.end(),
                    [&](const PinDef& p){ return p.name == currentPinName; }),
                    pins.end());
            }
            continue;
        }

        // ── Direction ─────────────────────────────────────────────────────────
        if (token == "DIRECTION" && !currentPinName.empty() && !skipPin) {
            std::string dir;
            iss >> dir;
            for (auto& p : currentMacro->pins)
                if (p.name == currentPinName) { p.isOutput = (dir == "OUTPUT"); break; }
            continue;
        }

        // ── Rect (pin geometry) ───────────────────────────────────────────────
        if (token == "RECT" && !currentPinName.empty() && !skipPin) {
            double x1, y1, x2, y2;
            if (iss >> x1 >> y1 >> x2 >> y2) {
                // Use centre of first RECT encountered (subsequent RECTs override — last wins)
                for (auto& p : currentMacro->pins)
                    if (p.name == currentPinName) {
                        p.dx = (x1 + x2) / 2.0;
                        p.dy = (y1 + y2) / 2.0;
                        break;
                    }
            }
            continue;
        }

        // ── End ───────────────────────────────────────────────────────────────
        if (token == "END") {
            std::string endName;
            iss >> endName;
            if (currentMacro && endName == currentMacro->name) {
                currentMacro   = nullptr;
                currentPinName = "";
                skipPin        = false;
            } else if (endName == currentPinName) {
                currentPinName = "";
                skipPin        = false;
            }
            continue;
        }
    }

    std::cout << "  [LEF] Library loaded successfully. Parsed geometry for macros/cells.\n";
}
