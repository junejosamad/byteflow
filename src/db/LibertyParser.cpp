#include "db/LibertyParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// Helper: trim whitespace from both ends
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n;");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Helper: parse a comma-separated list of doubles from a string like "10.0, 50.0, 200.0"
static std::vector<double> parseDoubleList(const std::string& s) {
    std::vector<double> result;
    std::string clean = s;
    // Remove quotes and parentheses
    for (char& c : clean) {
        if (c == '"' || c == '(' || c == ')' || c == '\\') c = ' ';
    }
    std::stringstream ss(clean);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (!t.empty()) {
            try { result.push_back(std::stod(t)); } catch (...) {}
        }
    }
    return result;
}

// Parse an NLDM table block (cell_rise, cell_fall, rise_transition, fall_transition)
static NldmTable parseNldmTable(std::ifstream& file) {
    NldmTable table;
    std::string line;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);

        // index_1("10.0, 50.0, 200.0");
        if (trimmed.find("index_1") != std::string::npos) {
            // Extract content between first ( and last )
            size_t p1 = line.find('(');
            size_t p2 = line.rfind(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
                table.index1 = parseDoubleList(line.substr(p1+1, p2-p1-1));
            }
        }
        // index_2("1.0, 5.0, 20.0");
        else if (trimmed.find("index_2") != std::string::npos) {
            size_t p1 = line.find('(');
            size_t p2 = line.rfind(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
                table.index2 = parseDoubleList(line.substr(p1+1, p2-p1-1));
            }
        }
        // values("15.0, 25.0, 55.0", \
        //        "20.0, 35.0, 75.0", ...);
        else if (trimmed.find("values") != std::string::npos) {
            // Collect all value lines until we see the closing ");"
            std::string valBlock;
            // Get content after "values("
            size_t vstart = line.find("values");
            if (vstart != std::string::npos) {
                valBlock += line.substr(vstart + 6);
            }
            // Continue reading if line ends with backslash or no closing paren
            while (valBlock.find(';') == std::string::npos) {
                std::string cont;
                if (!std::getline(file, cont)) break;
                valBlock += " " + cont;
            }
            // Now parse: split by quoted groups or by commas within quotes
            // Clean up: remove "values", "(", ")", ";", "\", quotes
            std::string clean = valBlock;
            for (char& c : clean) {
                if (c == '(' || c == ')' || c == ';' || c == '\\') c = ' ';
            }
            // Split by quotes: each quoted group is one row
            std::vector<std::vector<double>> rows;
            size_t pos = 0;
            while (pos < clean.size()) {
                size_t q1 = clean.find('"', pos);
                if (q1 == std::string::npos) break;
                size_t q2 = clean.find('"', q1+1);
                if (q2 == std::string::npos) break;
                std::string rowStr = clean.substr(q1+1, q2-q1-1);
                rows.push_back(parseDoubleList(rowStr));
                pos = q2 + 1;
            }
            // Fallback: if no quotes found, treat entire block as single-row
            if (rows.empty()) {
                auto vals = parseDoubleList(clean);
                if (!vals.empty()) rows.push_back(vals);
            }
            table.values = rows;
        }
        // End of this table block
        else if (trimmed.find("}") != std::string::npos) {
            break;
        }
    }
    return table;
}

void LibertyParser::parse(std::string filename, Library& lib) {
    std::cout << "  [Liberty] Loading library " << filename << "...\n";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open .lib file!\n";
        return;
    }

    std::string line, token;
    CellDef* currentCell = nullptr;
    std::string currentPinName = "";
    bool inTimingBlock = false;
    TimingArc currentArc;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        std::stringstream ss(trimmed);
        ss >> token;

        // 1. Cell definition: cell(AND2) {
        if (token.find("cell(") != std::string::npos) {
            std::string cellName = token.substr(5);
            // Remove trailing ) and {
            size_t paren = cellName.find(')');
            if (paren != std::string::npos) cellName = cellName.substr(0, paren);

            currentCell = lib.getCell(cellName);
            if (!currentCell) {
                currentCell = new CellDef();
                currentCell->name = cellName;
                currentCell->width = 1.0;
                currentCell->height = 1.0;
                lib.addCell(currentCell);
            }
            currentPinName = "";
            inTimingBlock = false;
        }
        // 2. Intrinsic delay (legacy scalar)
        else if (token == "intrinsic_rise" && currentCell) {
            std::string colon, valueStr;
            ss >> colon >> valueStr;
            if (!valueStr.empty() && valueStr.back() == ';') valueStr.pop_back();
            try { currentCell->intrinsicDelay = std::stod(valueStr); } catch (...) {}
        }
        // 3. Area
        else if (token == "area" && currentCell) {
            // Handle both "area : 4;" and "area: 4;" formats
            std::string rest;
            std::getline(ss, rest);
            std::string combined = token + rest;
            // Find the numeric value after ':' 
            size_t colonPos = combined.find(':');
            if (colonPos != std::string::npos) {
                std::string valueStr = trim(combined.substr(colonPos + 1));
                if (!valueStr.empty() && valueStr.back() == ';') valueStr.pop_back();
                valueStr = trim(valueStr);
                try {
                    double area = std::stod(valueStr);
                    currentCell->area = area;
                    currentCell->width = std::sqrt(area);
                    currentCell->height = std::sqrt(area);
                } catch (...) {}
            }
        }
        // 4. Pin definition: pin(A) { ... }
        else if (token.find("pin(") != std::string::npos && currentCell) {
            std::string pinName = token.substr(4);
            size_t paren = pinName.find(')');
            if (paren != std::string::npos) pinName = pinName.substr(0, paren);
            currentPinName = pinName;

            // Check if pin already exists
            bool exists = false;
            for (auto& p : currentCell->pins) if (p.name == pinName) exists = true;

            // Single-line pin definition: pin(A) { direction: input; }
            if (trimmed.find('}') != std::string::npos) {
                if (!exists) {
                    bool isOutput = (trimmed.find("output") != std::string::npos);
                    int numInputs = 0, numOutputs = 0;
                    for (auto& p : currentCell->pins) {
                        if (p.isOutput) numOutputs++;
                        else numInputs++;
                    }
                    double dx = isOutput ? (currentCell->width / 2.0) : -(currentCell->width / 2.0);
                    double dy = isOutput ? (2.0 + numOutputs * 2.0) : (2.0 + numInputs * 2.0);
                    currentCell->pins.push_back({ pinName, isOutput, 0.004, dx, dy });
                }
                // Single-line pin: no timing blocks possible, done
            }
            else {
                // Multi-line pin block: parse contents with brace-depth tracking
                // CRITICAL: We must enter this block even if pin already exists,
                // because timing() blocks are nested inside and contain NLDM data.
                int braceDepth = 1; // We've already seen the opening {
                bool isOutput = false;
                double cap = 0.004;

                while (std::getline(file, line) && braceDepth > 0) {
                    std::string subTrimmed = trim(line);

                    // Track brace depth
                    for (char c : line) {
                        if (c == '{') braceDepth++;
                        if (c == '}') braceDepth--;
                    }

                    // Only process direct pin properties at depth 1
                    if (braceDepth >= 1) {
                        if (subTrimmed.find("direction") != std::string::npos &&
                            subTrimmed.find("output") != std::string::npos) {
                            isOutput = true;
                        }
                        if (subTrimmed.find("capacitance") != std::string::npos &&
                            subTrimmed.find("timing") == std::string::npos) {
                            size_t colonPos = subTrimmed.find(':');
                            if (colonPos != std::string::npos) {
                                std::string capStr = trim(subTrimmed.substr(colonPos + 1));
                                if (!capStr.empty() && capStr.back() == ';') capStr.pop_back();
                                try { cap = std::stod(trim(capStr)); } catch (...) {}
                            }
                        }

                        // PHASE 5: Detect timing() block start
                        std::stringstream subSs(subTrimmed);
                        std::string subToken;
                        subSs >> subToken;

                        if (subToken.find("timing") != std::string::npos &&
                            subTrimmed.find("{") != std::string::npos) {
                            // Parse the timing block
                            inTimingBlock = true;
                            currentArc = TimingArc();
                            currentArc.toPin = currentPinName;

                            int timingBraceDepth = 1;
                            while (std::getline(file, line) && timingBraceDepth > 0) {
                                std::string tLine = trim(line);

                                // Track brace depth within timing block
                                for (char c : line) {
                                    if (c == '{') timingBraceDepth++;
                                    if (c == '}') timingBraceDepth--;
                                }

                                std::stringstream tSs(tLine);
                                std::string tToken;
                                tSs >> tToken;

                                if (tToken == "related_pin") {
                                    size_t q1 = tLine.find('"');
                                    size_t q2 = tLine.rfind('"');
                                    if (q1 != std::string::npos && q2 != q1) {
                                        currentArc.fromPin = tLine.substr(q1 + 1, q2 - q1 - 1);
                                    }
                                }
                                else if (tToken == "setup_constraint") {
                                    size_t colonPos = tLine.find(':');
                                    if (colonPos != std::string::npos) {
                                        std::string val = trim(tLine.substr(colonPos + 1));
                                        if (!val.empty() && val.back() == ';') val.pop_back();
                                        try { currentArc.setupTime = std::stod(trim(val)); } catch (...) {}
                                    }
                                }
                                else if (tToken == "hold_constraint") {
                                    size_t colonPos = tLine.find(':');
                                    if (colonPos != std::string::npos) {
                                        std::string val = trim(tLine.substr(colonPos + 1));
                                        if (!val.empty() && val.back() == ';') val.pop_back();
                                        try { currentArc.holdTime = std::stod(trim(val)); } catch (...) {}
                                    }
                                }
                                else if (tToken.find("cell_rise") != std::string::npos) {
                                    currentArc.cellRise = parseNldmTable(file);
                                    timingBraceDepth--; // parseNldmTable consumed the }
                                }
                                else if (tToken.find("cell_fall") != std::string::npos) {
                                    currentArc.cellFall = parseNldmTable(file);
                                    timingBraceDepth--;
                                }
                                else if (tToken.find("rise_transition") != std::string::npos) {
                                    currentArc.riseTransition = parseNldmTable(file);
                                    timingBraceDepth--;
                                }
                                else if (tToken.find("fall_transition") != std::string::npos) {
                                    currentArc.fallTransition = parseNldmTable(file);
                                    timingBraceDepth--;
                                }
                            }

                            // Commit timing arc
                            if (!currentArc.fromPin.empty() && !currentArc.toPin.empty()) {
                                currentCell->timingArcs.push_back(currentArc);
                            }
                            inTimingBlock = false;

                            // The closing } of timing block was consumed; adjust outer depth
                            braceDepth--;
                        }
                    }
                }

                // Add pin if it didn't exist
                if (!exists) {
                    int numInputs = 0, numOutputs = 0;
                    for (auto& p : currentCell->pins) {
                        if (p.isOutput) numOutputs++;
                        else numInputs++;
                    }
                    double dx = isOutput ? (currentCell->width / 2.0) : -(currentCell->width / 2.0);
                    double dy = isOutput ? (2.0 + numOutputs * 2.0) : (2.0 + numInputs * 2.0);
                    currentCell->pins.push_back({ pinName, isOutput, cap, dx, dy });
                }
            }
        }
        // 5. PHASE 5: Timing block inside a pin: timing() {
        else if (token.find("timing") != std::string::npos && currentCell && !currentPinName.empty() &&
                 trimmed.find("{") != std::string::npos) {
            inTimingBlock = true;
            currentArc = TimingArc();
            currentArc.toPin = currentPinName;
        }
        // 6. Related pin inside timing block: related_pin : "A";
        else if (token == "related_pin" && inTimingBlock) {
            size_t q1 = trimmed.find('"');
            size_t q2 = trimmed.rfind('"');
            if (q1 != std::string::npos && q2 != q1) {
                currentArc.fromPin = trimmed.substr(q1+1, q2-q1-1);
            }
        }
        // 7. Setup constraint
        else if (token == "setup_constraint" && inTimingBlock) {
            std::string rest;
            std::getline(ss, rest);
            size_t colonPos = rest.find(':');
            if (colonPos != std::string::npos) {
                std::string val = trim(rest.substr(colonPos + 1));
                if (!val.empty() && val.back() == ';') val.pop_back();
                try { currentArc.setupTime = std::stod(trim(val)); } catch (...) {}
            }
        }
        // 8. Hold constraint
        else if (token == "hold_constraint" && inTimingBlock) {
            std::string rest;
            std::getline(ss, rest);
            size_t colonPos = rest.find(':');
            if (colonPos != std::string::npos) {
                std::string val = trim(rest.substr(colonPos + 1));
                if (!val.empty() && val.back() == ';') val.pop_back();
                try { currentArc.holdTime = std::stod(trim(val)); } catch (...) {}
            }
        }
        // 9. NLDM table blocks
        else if (token.find("cell_rise") != std::string::npos && inTimingBlock) {
            currentArc.cellRise = parseNldmTable(file);
        }
        else if (token.find("cell_fall") != std::string::npos && inTimingBlock) {
            currentArc.cellFall = parseNldmTable(file);
        }
        else if (token.find("rise_transition") != std::string::npos && inTimingBlock) {
            currentArc.riseTransition = parseNldmTable(file);
        }
        else if (token.find("fall_transition") != std::string::npos && inTimingBlock) {
            currentArc.fallTransition = parseNldmTable(file);
        }
        // 10. End of timing block
        else if (trimmed == "}" && inTimingBlock) {
            // Commit the timing arc
            if (!currentArc.fromPin.empty() && !currentArc.toPin.empty()) {
                currentCell->timingArcs.push_back(currentArc);
            }
            inTimingBlock = false;
        }
    }

    // Post-load: update intrinsicDelay from NLDM tables for backward compatibility
    for (auto& [name, cell] : lib.cells) {
        if (!cell->timingArcs.empty()) {
            double maxDelay = 0.0;
            for (const auto& arc : cell->timingArcs) {
                double d = arc.getDelay(50.0, 5.0); // Nominal conditions
                if (d > maxDelay) maxDelay = d;
            }
            if (maxDelay > 0) {
                cell->intrinsicDelay = maxDelay;
                cell->delay = maxDelay;
            }
        }
    }

    std::cout << "  [Liberty] Library loaded. Parsed " << lib.cells.size() << " cells.\n";
    // Report NLDM status
    int nldmCells = 0;
    for (auto& [name, cell] : lib.cells) {
        if (!cell->timingArcs.empty()) nldmCells++;
    }
    std::cout << "  [Liberty] NLDM timing arcs: " << nldmCells << "/" << lib.cells.size() << " cells.\n";
}
