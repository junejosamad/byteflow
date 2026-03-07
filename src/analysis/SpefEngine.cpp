#include "analysis/SpefEngine.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <ctime>

// Initialize static constexpr members
constexpr double SpefEngine::layerSheetR[4];
constexpr double SpefEngine::layerCapPerUm[4];

// ============================================================
// EXTRACT: Sweep all routed nets and compute per-segment R/C
// ============================================================
void SpefEngine::extract(Design& design) {
    std::cout << "\n=== RC PARASITIC EXTRACTION ===\n";
    parasiticsMap.clear();

    int totalNets = 0;
    int extractedNets = 0;
    int totalSegments = 0;
    int totalVias = 0;

    for (Net* net : design.nets) {
        // Skip power nets (handled separately) and unrouted nets
        if (net->name == "VDD" || net->name == "VSS") continue;
        if (net->routePath.size() < 2) continue;
        totalNets++;

        NetParasitics np;
        np.netName = net->name;

        // Walk the route path in segment pairs (point[i], point[i+1])
        for (size_t i = 0; i + 1 < net->routePath.size(); i += 2) {
            const Point& p1 = net->routePath[i];
            const Point& p2 = net->routePath[i + 1];

            // Check for via (layer change)
            if (p1.layer != p2.layer) {
                // Via transition: add via resistance and capacitance
                WireSegment via;
                via.x1 = p1.x; via.y1 = p1.y;
                via.x2 = p2.x; via.y2 = p2.y;
                via.layer = std::min(p1.layer, p2.layer); // Via between layers
                via.length = 0.0;
                via.resistance = viaRes;
                via.capacitance = viaCap;
                np.segments.push_back(via);
                np.totalR += via.resistance;
                np.totalC += via.capacitance;
                totalVias++;
                continue;
            }

            // Same-layer wire segment
            int layer = p1.layer;
            if (layer < 0 || layer >= 4) layer = 1; // Safety clamp

            // Manhattan distance in grid units, convert to microns
            double gridDist = std::abs(p2.x - p1.x) + std::abs(p2.y - p1.y);
            double lengthUm = gridDist * gridPitchUm;

            if (lengthUm < 1e-6) continue; // Skip zero-length segments

            // R = (sheet_R / width) * length = (rho_sq) * (L / W)
            double numSquares = lengthUm / wireWidthUm;
            double segR = layerSheetR[layer] * numSquares;

            // C = cap_per_um * length (area capacitance to substrate + fringe)
            double segC = layerCapPerUm[layer] * lengthUm;

            WireSegment seg;
            seg.x1 = p1.x; seg.y1 = p1.y;
            seg.x2 = p2.x; seg.y2 = p2.y;
            seg.layer = layer;
            seg.length = lengthUm;
            seg.resistance = segR;
            seg.capacitance = segC;

            np.segments.push_back(seg);
            np.totalR += segR;
            np.totalC += segC;
            totalSegments++;
        }

        // ============================================================
        // ELMORE DELAY CALCULATION
        // For a simple RC chain: tau = sum(Ri * Ci_downstream)
        // This is the dominant delay model for on-chip interconnect.
        // ============================================================
        if (!np.segments.empty()) {
            double elmore = 0.0;
            double runningR = 0.0;

            // Elmore delay: each segment's R contributes to delay of all downstream C
            // tau = R1*C_total + (R1+R2)*C_from_seg2_on + ...
            // Simplified: tau = sum_i( R_i * sum_j>=i(C_j) )
            for (size_t i = 0; i < np.segments.size(); ++i) {
                runningR += np.segments[i].resistance;
                // Downstream capacitance from this segment onward
                double downstreamC = 0.0;
                for (size_t j = i; j < np.segments.size(); ++j) {
                    downstreamC += np.segments[j].capacitance;
                }
                elmore += np.segments[i].resistance * downstreamC;
            }

            // Convert to ps: Elmore delay = R(ohms) * C(fF) = τ in femtoseconds
            // 1 ps = 1000 fs, so divide by 1000
            // Actually: R(Ω) × C(fF) = R × C × 10^-15 seconds = RC × 10^-3 ps
            // So multiply by 0.001 to get ps
            np.elmoreDelay = elmore * 0.001; // Convert Ω·fF to ps
        }

        parasiticsMap[net->name] = np;
        extractedNets++;
    }

    // Report summary
    std::cout << "  [SpefEngine] Extracted " << extractedNets << "/" << totalNets << " nets.\n";
    std::cout << "  [SpefEngine] Total wire segments: " << totalSegments << "\n";
    std::cout << "  [SpefEngine] Total vias: " << totalVias << "\n";

    // Report top 5 worst RC nets
    std::vector<std::pair<std::string, double>> rcList;
    for (auto& [name, np] : parasiticsMap) {
        rcList.push_back({name, np.elmoreDelay});
    }
    std::sort(rcList.begin(), rcList.end(), [](auto& a, auto& b) { return a.second > b.second; });

    std::cout << "\n  --- Top RC Parasitics (Worst Elmore Delay) ---\n";
    std::cout << "  " << std::left << std::setw(25) << "Net"
              << std::setw(12) << "R (ohms)"
              << std::setw(12) << "C (fF)"
              << std::setw(15) << "Elmore (ps)" << "\n";
    std::cout << "  " << std::string(64, '-') << "\n";

    int shown = 0;
    for (auto& [name, delay] : rcList) {
        if (shown >= 5) break;
        auto& np = parasiticsMap[name];
        std::cout << "  " << std::left << std::setw(25) << name
                  << std::fixed << std::setprecision(2)
                  << std::setw(12) << np.totalR
                  << std::setw(12) << np.totalC
                  << std::setw(15) << np.elmoreDelay << "\n";
        shown++;
    }
}

// ============================================================
// SPEF EXPORT: IEEE Standard Parasitic Exchange Format
// ============================================================
void SpefEngine::writeSpef(const std::string& filename, Design& design) {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "  [SpefEngine] Error: Cannot write SPEF file: " << filename << "\n";
        return;
    }

    // Get current timestamp
    std::time_t now = std::time(nullptr);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%a %b %d %H:%M:%S %Y", std::localtime(&now));

    // ---- HEADER SECTION ----
    out << "*SPEF \"IEEE 1481-2009\"\n";
    out << "*DESIGN \"" << design.name << "\"\n";
    out << "*DATE \"" << timeBuf << "\"\n";
    out << "*VENDOR \"Byteflow EDA\"\n";
    out << "*PROGRAM \"Byteflow SpefEngine\"\n";
    out << "*VERSION \"5.0\"\n";
    out << "*DESIGN_FLOW \"NETLIST_TYPE_VERILOG\"\n";
    out << "*DIVIDER /\n";
    out << "*DELIMITER :\n";
    out << "*BUS_DELIMITER [ ]\n";
    out << "*T_UNIT 1 PS\n";
    out << "*C_UNIT 1 FF\n";
    out << "*R_UNIT 1 OHM\n";
    out << "*L_UNIT 1 HENRY\n";
    out << "\n";

    // ---- NAME MAP ----
    out << "*NAME_MAP\n";
    int nameIdx = 1;
    std::unordered_map<std::string, int> nameMap;
    for (auto& [netName, np] : parasiticsMap) {
        nameMap[netName] = nameIdx;
        out << "*" << nameIdx << " " << netName << "\n";
        nameIdx++;
    }
    out << "\n";

    // ---- PORT SECTION (Primary I/O — simplified for now) ----
    out << "*PORTS\n";
    // We skip detailed port definitions for this version
    out << "\n";

    // ---- D_NET SECTIONS (One per extracted net) ----
    for (auto& [netName, np] : parasiticsMap) {
        int nid = nameMap[netName];

        out << "*D_NET *" << nid << " " 
            << std::fixed << std::setprecision(4) << np.totalC << "\n";

        // *CONN section
        out << "*CONN\n";
        // Find the actual net's connected pins
        for (Net* net : design.nets) {
            if (net->name == netName) {
                for (Pin* pin : net->connectedPins) {
                    if (!pin || !pin->inst) continue;
                    std::string pinPath = pin->inst->name + ":" + pin->name;
                    char dir = (pin->type == PinType::OUTPUT) ? 'O' : 'I';
                    out << "*" << dir << " " << pinPath << "\n";
                }
                break;
            }
        }

        // *CAP section (lumped capacitance per segment endpoint)
        out << "*CAP\n";
        int capIdx = 1;
        for (const auto& seg : np.segments) {
            // Distribute cap to both endpoints (half each)
            std::string node1 = "*" + std::to_string(nid) + ":" + std::to_string(capIdx);
            out << capIdx << " " << node1 << " "
                << std::fixed << std::setprecision(6) << seg.capacitance << "\n";
            capIdx++;
        }

        // *RES section
        out << "*RES\n";
        int resIdx = 1;
        for (const auto& seg : np.segments) {
            std::string node1 = "*" + std::to_string(nid) + ":" + std::to_string(resIdx);
            std::string node2 = "*" + std::to_string(nid) + ":" + std::to_string(resIdx + 1);
            out << resIdx << " " << node1 << " " << node2 << " "
                << std::fixed << std::setprecision(6) << seg.resistance << "\n";
            resIdx++;
        }

        out << "*END\n\n";
    }

    out.close();
    std::cout << "  [SpefEngine] SPEF written to " << filename 
              << " (" << parasiticsMap.size() << " nets)\n";
}

// ============================================================
// QUERY API
// ============================================================
const NetParasitics* SpefEngine::getParasitics(const std::string& netName) const {
    auto it = parasiticsMap.find(netName);
    return (it != parasiticsMap.end()) ? &it->second : nullptr;
}

double SpefEngine::getWireDelay(const std::string& netName) const {
    auto it = parasiticsMap.find(netName);
    return (it != parasiticsMap.end()) ? it->second.elmoreDelay : 0.0;
}

double SpefEngine::getNetCap(const std::string& netName) const {
    auto it = parasiticsMap.find(netName);
    return (it != parasiticsMap.end()) ? it->second.totalC : 0.0;
}
