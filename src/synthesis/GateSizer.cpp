#include "synthesis/GateSizer.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

// ============================================================
// Helpers — drive-strength variant lookup
// ============================================================

// Simple.lib style:  AND2 → AND2_X2 → AND2_X4
// Sky130 style:      sky130_fd_sc_hd__inv_2 → inv_4
// Both use a trailing numeric suffix separated by '_'.

static int parseStrengthSuffix(const std::string& name, std::string& base) {
    // Check for _XN pattern first (simple.lib _X2, _X4 …)
    auto xpos = name.rfind("_X");
    if (xpos != std::string::npos) {
        const std::string tail = name.substr(xpos + 2);  // after "_X"
        try {
            int s = std::stoi(tail);
            if (s > 0) { base = name.substr(0, xpos); return s; }
        } catch (...) {}
    }
    // Fall back to sky130 _N pattern
    auto pos = name.rfind('_');
    if (pos != std::string::npos && pos + 1 < name.size()) {
        try {
            int s = std::stoi(name.substr(pos + 1));
            if (s > 0) { base = name.substr(0, pos); return s; }
        } catch (...) {}
    }
    base = name;
    return 1;  // baseline strength
}

CellDef* GateSizer::findUpsizedVariant(Library* lib, CellDef* current) {
    if (!lib || !current) return nullptr;

    std::string base;
    int curStr = parseStrengthSuffix(current->name, base);

    // Try _XN multiples for simple.lib style
    static const int mults[] = {2, 4, 8};
    for (int m : mults) {
        if (m <= curStr) continue;
        std::string cand = base + "_X" + std::to_string(m);
        CellDef* c = lib->getCell(cand);
        if (c && !c->isSequential) return c;
    }

    // Try sky130 _N multiples
    static const int strengths[] = {2, 4, 6, 8, 12, 16};
    for (int s : strengths) {
        if (s <= curStr) continue;
        std::string cand = base + "_" + std::to_string(s);
        CellDef* c = lib->getCell(cand);
        if (c && !c->isSequential) return c;
    }

    return nullptr;
}

CellDef* GateSizer::findDownsizedVariant(Library* lib, CellDef* current) {
    if (!lib || !current) return nullptr;

    std::string base;
    int curStr = parseStrengthSuffix(current->name, base);

    if (curStr <= 1) return nullptr;  // already at minimum

    // Try _XN in descending order (largest smaller than curStr)
    static const int mults[] = {4, 2, 1};
    for (int m : mults) {
        if (m >= curStr) continue;
        std::string cand = (m == 1) ? base : (base + "_X" + std::to_string(m));
        CellDef* c = lib->getCell(cand);
        if (c && !c->isSequential) return c;
    }

    // Sky130 fallback
    static const int strengths[] = {8, 6, 4, 2, 1};
    for (int s : strengths) {
        if (s >= curStr) continue;
        std::string cand = (s == 1) ? base : (base + "_" + std::to_string(s));
        CellDef* c = lib->getCell(cand);
        if (c && !c->isSequential) return c;
    }

    return nullptr;
}

double GateSizer::cellArea(CellDef* cell) {
    return cell ? cell->area : 0.0;
}

// ============================================================
// resizeForTiming
// ============================================================

GateSizeResult GateSizer::resizeForTiming(Design* chip, Timer& timer,
                                           int maxChanges) {
    GateSizeResult res;
    if (!chip || !chip->cellLibrary) return res;

    timer.buildGraph();
    timer.updateTiming();
    double wnsBefore = timer.getWNS();

    // Collect non-sequential gates with negative setup slack on output pins
    struct Candidate { GateInstance* inst; double worstSlack; };
    std::vector<Candidate> violators;

    for (GateInstance* inst : chip->instances) {
        if (!inst->type || inst->type->isSequential) continue;
        double ws = 1e30;
        bool hasOutput = false;
        for (Pin* p : inst->pins) {
            if (p->type == PinType::OUTPUT) {
                hasOutput = true;
                if (p->slack < ws) ws = p->slack;
            }
        }
        if (!hasOutput || ws >= 1e29) continue;  // no observable output
        if (ws < 0.0)
            violators.push_back({inst, ws});
    }

    std::sort(violators.begin(), violators.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.worstSlack < b.worstSlack;
              });

    int attempts = std::min((int)violators.size(), maxChanges);
    for (int i = 0; i < attempts; i++) {
        GateInstance* inst = violators[i].inst;
        CellDef* larger = findUpsizedVariant(chip->cellLibrary, inst->type);
        if (!larger) continue;

        double areaOld = cellArea(inst->type);
        std::ostringstream msg;
        msg << "upsize " << inst->name << ": "
            << inst->type->name << " -> " << larger->name
            << " (slack=" << std::fixed << std::setprecision(1)
            << violators[i].worstSlack << " ps)";
        res.resizeLog.push_back(msg.str());
        std::cout << "  [GateSizer] " << msg.str() << "\n";

        inst->type = larger;
        res.areaSavedUnits -= (cellArea(larger) - areaOld);  // negative = grew
        res.cellsUpsized++;
    }

    if (res.cellsUpsized > 0) {
        timer.buildGraph();
        timer.updateTiming();
    }

    double wnsAfter = timer.getWNS();
    res.timingImprovementPs = wnsAfter - wnsBefore;

    std::cout << "  [GateSizer] resizeForTiming: " << res.cellsUpsized
              << " upsized, WNS " << std::fixed << std::setprecision(1)
              << wnsBefore << " -> " << wnsAfter << " ps\n";
    return res;
}

// ============================================================
// resizeForArea
// ============================================================

GateSizeResult GateSizer::resizeForArea(Design* chip, Timer& timer,
                                         double slackBudgetPs,
                                         int maxChanges) {
    GateSizeResult res;
    if (!chip || !chip->cellLibrary) return res;

    timer.buildGraph();
    timer.updateTiming();
    double wnsBefore = timer.getWNS();

    // Collect non-sequential gates whose minimum output slack exceeds
    // 2× the budget (conservative: downsizing adds ~35% delay, so we need
    // enough headroom).  Sort by most positive slack first.
    struct Candidate { GateInstance* inst; double minSlack; };
    std::vector<Candidate> candidates;

    const double threshold = slackBudgetPs * 2.0;
    for (GateInstance* inst : chip->instances) {
        if (!inst->type || inst->type->isSequential) continue;
        CellDef* smaller = findDownsizedVariant(chip->cellLibrary, inst->type);
        if (!smaller) continue;  // already minimum
        double ms = 1e30;
        bool hasOutputPin = false;
        for (Pin* p : inst->pins) {
            if (p->type == PinType::OUTPUT) {
                hasOutputPin = true;
                if (p->slack < ms) ms = p->slack;
            }
        }
        // Skip gates with no output pin, or whose slack was never written
        // (timer may leave slack=0 on unobservable nodes).
        if (!hasOutputPin || ms >= 1e29) continue;
        if (ms > threshold)
            candidates.push_back({inst, ms});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.minSlack > b.minSlack;
              });

    int attempts = std::min((int)candidates.size(), maxChanges);
    for (int i = 0; i < attempts; i++) {
        GateInstance* inst = candidates[i].inst;
        CellDef* smaller = findDownsizedVariant(chip->cellLibrary, inst->type);
        if (!smaller) continue;

        double areaOld = cellArea(inst->type);
        std::ostringstream msg;
        msg << "downsize " << inst->name << ": "
            << inst->type->name << " -> " << smaller->name
            << " (slack=" << std::fixed << std::setprecision(1)
            << candidates[i].minSlack << " ps)";
        res.resizeLog.push_back(msg.str());
        std::cout << "  [GateSizer] " << msg.str() << "\n";

        inst->type = smaller;
        res.areaSavedUnits += (areaOld - cellArea(smaller));
        res.cellsDownsized++;
    }

    if (res.cellsDownsized > 0) {
        timer.buildGraph();
        timer.updateTiming();
    }

    double wnsAfter = timer.getWNS();
    res.timingImprovementPs = wnsAfter - wnsBefore;

    std::cout << "  [GateSizer] resizeForArea: " << res.cellsDownsized
              << " downsized, area saved=" << std::fixed << std::setprecision(1)
              << res.areaSavedUnits << ", WNS "
              << wnsBefore << " -> " << wnsAfter << " ps\n";
    return res;
}
