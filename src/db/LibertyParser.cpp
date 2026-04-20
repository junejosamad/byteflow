#include "db/LibertyParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n;");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static std::vector<double> parseDoubleList(const std::string& s) {
    std::vector<double> result;
    std::string clean = s;
    for (char& c : clean)
        if (c == '"' || c == '(' || c == ')' || c == '\\') c = ' ';
    std::stringstream ss(clean);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::string t = trim(token);
        if (!t.empty()) try { result.push_back(std::stod(t)); } catch (...) {}
    }
    return result;
}

// Extract name from patterns like:  keyword(name)  keyword("name")  keyword ("name")
static std::string extractKeywordName(const std::string& trimmed, const std::string& keyword) {
    size_t kpos = trimmed.find(keyword);
    if (kpos == std::string::npos) return "";
    size_t pos = kpos + keyword.size();
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) pos++;
    if (pos >= trimmed.size() || trimmed[pos] != '(') return "";
    pos++; // skip '('
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) pos++;
    if (pos < trimmed.size() && trimmed[pos] == '"') {
        size_t q2 = trimmed.find('"', pos + 1);
        if (q2 != std::string::npos)
            return trimmed.substr(pos + 1, q2 - pos - 1);
    }
    size_t paren = trimmed.find(')', pos);
    if (paren != std::string::npos)
        return trimmed.substr(pos, paren - pos);
    return "";
}

// Skip a brace-delimited block; call after the opening '{' has been read on the current line.
// Returns when the matching '}' is consumed.
static void skipBlock(std::ifstream& file) {
    std::string line;
    int depth = 1;
    while (depth > 0 && std::getline(file, line)) {
        for (char c : line) {
            if (c == '{') depth++;
            else if (c == '}') { if (--depth == 0) return; }
        }
    }
}

static NldmTable parseNldmTable(std::ifstream& file) {
    NldmTable table;
    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.find("index_1") != std::string::npos) {
            size_t p1 = line.find('('), p2 = line.rfind(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1)
                table.index1 = parseDoubleList(line.substr(p1 + 1, p2 - p1 - 1));
        } else if (trimmed.find("index_2") != std::string::npos) {
            size_t p1 = line.find('('), p2 = line.rfind(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1)
                table.index2 = parseDoubleList(line.substr(p1 + 1, p2 - p1 - 1));
        } else if (trimmed.find("values") != std::string::npos) {
            std::string valBlock;
            size_t vstart = line.find("values");
            if (vstart != std::string::npos) valBlock += line.substr(vstart + 6);
            while (valBlock.find(';') == std::string::npos) {
                std::string cont;
                if (!std::getline(file, cont)) break;
                valBlock += " " + cont;
            }
            std::string clean = valBlock;
            for (char& c : clean)
                if (c == '(' || c == ')' || c == ';' || c == '\\') c = ' ';
            std::vector<std::vector<double>> rows;
            size_t pos = 0;
            while (pos < clean.size()) {
                size_t q1 = clean.find('"', pos);
                if (q1 == std::string::npos) break;
                size_t q2 = clean.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                rows.push_back(parseDoubleList(clean.substr(q1 + 1, q2 - q1 - 1)));
                pos = q2 + 1;
            }
            if (rows.empty()) {
                auto vals = parseDoubleList(clean);
                if (!vals.empty()) rows.push_back(vals);
            }
            table.values = rows;
        } else if (trimmed.find('}') != std::string::npos) {
            break;
        }
    }
    return table;
}

void LibertyParser::parse(std::string filename, Library& lib) {
    std::cout << "  [Liberty] Loading library " << filename << "...\n";
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "  [Liberty] Error: Could not open " << filename << "\n";
        return;
    }

    std::string line;
    CellDef*    currentCell     = nullptr;
    std::string currentPinName  = "";
    bool        inTimingBlock   = false;
    TimingArc   currentArc;
    double      timeUnitScale   = 1.0; // multiply parsed time values by this to get ps

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '/') continue;

        std::stringstream ss(trimmed);
        std::string token;
        ss >> token;

        // ── Library-level: time unit detection ───────────────────────────────
        if (token == "time_unit") {
            // time_unit : "1ns";   or   time_unit : "1ps";
            size_t q1 = trimmed.find('"'), q2 = trimmed.rfind('"');
            if (q1 != std::string::npos && q2 != q1) {
                std::string tu = trimmed.substr(q1 + 1, q2 - q1 - 1);
                if (tu.find("ns") != std::string::npos)      timeUnitScale = 1000.0; // ns → ps
                else if (tu.find("ps") != std::string::npos) timeUnitScale = 1.0;
            }
            continue;
        }

        // ── Cell definition ───────────────────────────────────────────────────
        // Handles both:  cell(NAME) {     and     cell ("NAME") {
        bool isCellToken = (token.find("cell(") != std::string::npos) ||
                           (token == "cell" && trimmed.find('(') != std::string::npos);
        // but NOT "cell_rise", "cell_fall", etc.
        if (isCellToken && token.find("cell_") == std::string::npos) {
            std::string cellName = extractKeywordName(trimmed, "cell");
            if (cellName.empty()) continue;

            currentCell = lib.getCell(cellName);
            if (!currentCell) {
                currentCell = new CellDef();
                currentCell->name = cellName;
                currentCell->width  = 1.0;
                currentCell->height = 1.0;
                lib.addCell(currentCell);
            }
            currentPinName = "";
            inTimingBlock  = false;
            continue;
        }

        if (!currentCell) continue; // nothing else matters outside a cell block

        // ── Skip power/ground pin blocks (pg_pin) ─────────────────────────────
        if (token == "pg_pin" && trimmed.find('{') != std::string::npos) {
            skipBlock(file);
            continue;
        }

        // ── Skip leakage_power / internal_power blocks ────────────────────────
        if ((token == "leakage_power" || token == "internal_power") &&
            trimmed.find('{') != std::string::npos) {
            skipBlock(file);
            continue;
        }

        // ── Area ──────────────────────────────────────────────────────────────
        if (token == "area") {
            size_t colonPos = trimmed.find(':');
            if (colonPos != std::string::npos) {
                std::string valueStr = trim(trimmed.substr(colonPos + 1));
                if (!valueStr.empty() && valueStr.back() == ';') valueStr.pop_back();
                try {
                    double area = std::stod(trim(valueStr));
                    currentCell->area   = area;
                    currentCell->width  = std::sqrt(area);
                    currentCell->height = std::sqrt(area);
                } catch (...) {}
            }
            continue;
        }

        // ── Intrinsic delay (legacy scalar) ───────────────────────────────────
        if (token == "intrinsic_rise") {
            std::string colon, valueStr;
            ss >> colon >> valueStr;
            if (!valueStr.empty() && valueStr.back() == ';') valueStr.pop_back();
            try { currentCell->intrinsicDelay = std::stod(valueStr) * timeUnitScale; } catch (...) {}
            continue;
        }

        // ── Pin definition ────────────────────────────────────────────────────
        // Handles:  pin(A) {     pin ("A") {
        bool isPinToken = (token.find("pin(") != std::string::npos) ||
                          (token == "pin" && trimmed.find('(') != std::string::npos);
        if (isPinToken) {
            std::string pinName = extractKeywordName(trimmed, "pin");
            if (pinName.empty()) continue;
            currentPinName = pinName;

            bool exists = false;
            for (auto& p : currentCell->pins) if (p.name == pinName) { exists = true; break; }

            // Single-line pin
            if (trimmed.find('}') != std::string::npos) {
                if (!exists) {
                    bool isOut = (trimmed.find("output") != std::string::npos);
                    int ni = 0, no = 0;
                    for (auto& p : currentCell->pins) { if (p.isOutput) no++; else ni++; }
                    double dx = isOut ? (currentCell->width / 2.0) : -(currentCell->width / 2.0);
                    double dy = isOut ? (2.0 + no * 2.0) : (2.0 + ni * 2.0);
                    currentCell->pins.push_back({ pinName, isOut, 0.004, dx, dy });
                }
                continue;
            }

            // Multi-line pin block
            int braceDepth = 1;
            bool isOutput  = false;
            double cap     = 0.004;

            while (std::getline(file, line) && braceDepth > 0) {
                std::string sub = trim(line);
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    if (c == '}') braceDepth--;
                }

                if (braceDepth >= 1) {
                    if (sub.find("direction") != std::string::npos &&
                        sub.find("output")    != std::string::npos)
                        isOutput = true;

                    if (sub.find("capacitance") != std::string::npos &&
                        sub.find("timing")      == std::string::npos) {
                        size_t cp = sub.find(':');
                        if (cp != std::string::npos) {
                            std::string cs = trim(sub.substr(cp + 1));
                            if (!cs.empty() && cs.back() == ';') cs.pop_back();
                            try { cap = std::stod(trim(cs)); } catch (...) {}
                        }
                    }

                    std::stringstream subSs(sub);
                    std::string subToken;
                    subSs >> subToken;

                    // Skip internal_power inside pin block
                    if (subToken == "internal_power" && sub.find('{') != std::string::npos) {
                        skipBlock(file);
                        braceDepth--; // skipBlock consumed the closing brace
                        continue;
                    }

                    // Parse timing() block inside pin
                    if (subToken.find("timing") != std::string::npos &&
                        sub.find('{')           != std::string::npos) {
                        inTimingBlock = true;
                        currentArc    = TimingArc();
                        currentArc.toPin = currentPinName;

                        int td = 1;
                        while (std::getline(file, line) && td > 0) {
                            std::string tLine = trim(line);
                            for (char c : line) {
                                if (c == '{') td++;
                                if (c == '}') td--;
                            }
                            std::stringstream tSs(tLine);
                            std::string tTok;
                            tSs >> tTok;

                            if (tTok == "related_pin") {
                                size_t q1 = tLine.find('"'), q2 = tLine.rfind('"');
                                if (q1 != std::string::npos && q2 != q1)
                                    currentArc.fromPin = tLine.substr(q1 + 1, q2 - q1 - 1);
                            } else if (tTok == "setup_constraint") {
                                size_t cp = tLine.find(':');
                                if (cp != std::string::npos) {
                                    std::string v = trim(tLine.substr(cp + 1));
                                    if (!v.empty() && v.back() == ';') v.pop_back();
                                    try { currentArc.setupTime = std::stod(trim(v)) * timeUnitScale; } catch (...) {}
                                }
                            } else if (tTok == "hold_constraint") {
                                size_t cp = tLine.find(':');
                                if (cp != std::string::npos) {
                                    std::string v = trim(tLine.substr(cp + 1));
                                    if (!v.empty() && v.back() == ';') v.pop_back();
                                    try { currentArc.holdTime = std::stod(trim(v)) * timeUnitScale; } catch (...) {}
                                }
                            } else if (tTok.find("cell_rise")        != std::string::npos) {
                                currentArc.cellRise       = parseNldmTable(file); td--;
                            } else if (tTok.find("cell_fall")        != std::string::npos) {
                                currentArc.cellFall       = parseNldmTable(file); td--;
                            } else if (tTok.find("rise_transition")  != std::string::npos) {
                                currentArc.riseTransition = parseNldmTable(file); td--;
                            } else if (tTok.find("fall_transition")  != std::string::npos) {
                                currentArc.fallTransition = parseNldmTable(file); td--;
                            }
                        }

                        if (!currentArc.fromPin.empty() && !currentArc.toPin.empty())
                            currentCell->timingArcs.push_back(currentArc);
                        inTimingBlock = false;
                        braceDepth--;
                    }
                }
            }

            if (!exists) {
                int ni = 0, no = 0;
                for (auto& p : currentCell->pins) { if (p.isOutput) no++; else ni++; }
                double dx = isOutput ? (currentCell->width / 2.0) : -(currentCell->width / 2.0);
                double dy = isOutput ? (2.0 + no * 2.0) : (2.0 + ni * 2.0);
                currentCell->pins.push_back({ pinName, isOutput, cap, dx, dy });
            }
            continue;
        }

        // ── Timing block at cell scope (outside explicit pin block) ───────────
        if (token.find("timing") != std::string::npos && !currentPinName.empty() &&
            trimmed.find('{')    != std::string::npos) {
            inTimingBlock    = true;
            currentArc       = TimingArc();
            currentArc.toPin = currentPinName;
            continue;
        }
        if (token == "related_pin" && inTimingBlock) {
            size_t q1 = trimmed.find('"'), q2 = trimmed.rfind('"');
            if (q1 != std::string::npos && q2 != q1)
                currentArc.fromPin = trimmed.substr(q1 + 1, q2 - q1 - 1);
            continue;
        }
        if (token == "setup_constraint" && inTimingBlock) {
            size_t cp = trimmed.find(':');
            if (cp != std::string::npos) {
                std::string v = trim(trimmed.substr(cp + 1));
                if (!v.empty() && v.back() == ';') v.pop_back();
                try { currentArc.setupTime = std::stod(trim(v)) * timeUnitScale; } catch (...) {}
            }
            continue;
        }
        if (token == "hold_constraint" && inTimingBlock) {
            size_t cp = trimmed.find(':');
            if (cp != std::string::npos) {
                std::string v = trim(trimmed.substr(cp + 1));
                if (!v.empty() && v.back() == ';') v.pop_back();
                try { currentArc.holdTime = std::stod(trim(v)) * timeUnitScale; } catch (...) {}
            }
            continue;
        }
        if (token.find("cell_rise")       != std::string::npos && inTimingBlock) {
            currentArc.cellRise       = parseNldmTable(file); continue;
        }
        if (token.find("cell_fall")       != std::string::npos && inTimingBlock) {
            currentArc.cellFall       = parseNldmTable(file); continue;
        }
        if (token.find("rise_transition") != std::string::npos && inTimingBlock) {
            currentArc.riseTransition = parseNldmTable(file); continue;
        }
        if (token.find("fall_transition") != std::string::npos && inTimingBlock) {
            currentArc.fallTransition = parseNldmTable(file); continue;
        }
        if (trimmed == "}" && inTimingBlock) {
            if (!currentArc.fromPin.empty() && !currentArc.toPin.empty())
                currentCell->timingArcs.push_back(currentArc);
            inTimingBlock = false;
            continue;
        }
    }

    // Post-load: update intrinsicDelay from NLDM tables
    for (auto& [name, cell] : lib.cells) {
        if (!cell->timingArcs.empty()) {
            double maxDelay = 0.0;
            for (const auto& arc : cell->timingArcs) {
                double d = arc.getDelay(50.0, 5.0);
                if (d > maxDelay) maxDelay = d;
            }
            if (maxDelay > 0) {
                cell->intrinsicDelay = maxDelay;
                cell->delay          = maxDelay;
            }
        }
    }

    std::cout << "  [Liberty] Library loaded. Parsed " << lib.cells.size() << " cells.\n";
    int nldmCells = 0;
    for (auto& [name, cell] : lib.cells)
        if (!cell->timingArcs.empty()) nldmCells++;
    std::cout << "  [Liberty] NLDM timing arcs: " << nldmCells << "/" << lib.cells.size() << " cells.\n";
}
