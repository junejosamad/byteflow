#include "analysis/LvsEngine.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <limits>

// ============================================================
// LvsReport counters
// ============================================================

int LvsReport::unplacedCount() const {
    int n = 0;
    for (const auto& m : mismatches)
        if (m.type == LvsMismatchType::UNPLACED_INSTANCE) n++;
    return n;
}
int LvsReport::unconnectedPinCount() const {
    int n = 0;
    for (const auto& m : mismatches)
        if (m.type == LvsMismatchType::UNCONNECTED_PIN) n++;
    return n;
}
int LvsReport::unroutedCount() const {
    int n = 0;
    for (const auto& m : mismatches)
        if (m.type == LvsMismatchType::UNROUTED_NET) n++;
    return n;
}
int LvsReport::openCircuitCount() const {
    int n = 0;
    for (const auto& m : mismatches)
        if (m.type == LvsMismatchType::OPEN_CIRCUIT) n++;
    return n;
}

void LvsReport::print(int maxPrint) const {
    std::cout << "\n=== LVS REPORT ===\n";
    std::cout << "  Instances       : " << instanceCount     << "\n";
    std::cout << "  Nets            : " << netCount          << "\n";
    std::cout << "  Routed nets     : " << routedNetCount    << "\n";
    std::cout << "  Total pins      : " << totalPinCount     << "\n";
    std::cout << "  Connected pins  : " << connectedPinCount << "\n";
    std::cout << "  --- Mismatches ---\n";
    std::cout << "  Unplaced cells  : " << unplacedCount()       << "\n";
    std::cout << "  Unconnected pins: " << unconnectedPinCount() << "\n";
    std::cout << "  Unrouted nets   : " << unroutedCount()       << "\n";
    std::cout << "  Open circuits   : " << openCircuitCount()    << "\n";
    std::cout << "  Total           : " << totalCount()          << "\n";

    if (mismatches.empty()) {
        std::cout << "  LVS PASSED — layout matches schematic.\n";
    } else {
        std::cout << "\n  Top mismatches (up to " << maxPrint << "):\n";
        int shown = 0;
        for (const auto& m : mismatches) {
            if (shown++ >= maxPrint) break;
            const char* typeName = "?";
            switch (m.type) {
                case LvsMismatchType::UNPLACED_INSTANCE: typeName = "UNPLACED";  break;
                case LvsMismatchType::UNCONNECTED_PIN:   typeName = "FLOAT_PIN"; break;
                case LvsMismatchType::UNROUTED_NET:      typeName = "UNROUTED";  break;
                case LvsMismatchType::OPEN_CIRCUIT:      typeName = "OPEN";      break;
            }
            std::cout << "  [" << typeName << "]";
            if (!m.instName.empty()) std::cout << " inst=" << m.instName;
            if (!m.netName.empty())  std::cout << " net="  << m.netName;
            if (!m.pinName.empty())  std::cout << " pin="  << m.pinName;
            std::cout << "  " << m.message << "\n";
        }
        if (totalCount() > maxPrint)
            std::cout << "  ... " << (totalCount() - maxPrint) << " more\n";
    }
    std::cout << "==================\n";
}

// ============================================================
// Check 1: every instance must be placed
// ============================================================
void LvsEngine::checkPlacement(Design* chip, LvsReport& report) const {
    for (GateInstance* inst : chip->instances) {
        if (!inst->isPlaced) {
            LvsMismatch m;
            m.type     = LvsMismatchType::UNPLACED_INSTANCE;
            m.instName = inst->name;
            m.message  = "instance not placed after run_placement";
            report.mismatches.push_back(m);
        }
    }
}

// ============================================================
// Check 2: every pin must have a net
// ============================================================
void LvsEngine::checkPinConnections(Design* chip, LvsReport& report) const {
    constexpr int CAP = 200;
    int found = 0;
    for (GateInstance* inst : chip->instances) {
        for (Pin* p : inst->pins) {
            report.totalPinCount++;
            if (p->net != nullptr) {
                report.connectedPinCount++;
            } else if (found < CAP) {
                LvsMismatch m;
                m.type     = LvsMismatchType::UNCONNECTED_PIN;
                m.instName = inst->name;
                m.pinName  = p->name;
                m.message  = "pin has no net connection";
                report.mismatches.push_back(m);
                found++;
            }
        }
    }
}

// ============================================================
// Check 3: multi-pin nets must have routing
// ============================================================
void LvsEngine::checkNetRouting(Design* chip, LvsReport& report) const {
    constexpr int CAP = 200;
    int found = 0;
    for (Net* net : chip->nets) {
        if ((int)net->connectedPins.size() < 2) continue;  // single-driver stub, skip
        if (net->routePath.size() < 2 && found < CAP) {
            LvsMismatch m;
            m.type    = LvsMismatchType::UNROUTED_NET;
            m.netName = net->name;
            m.message = std::to_string(net->connectedPins.size())
                      + " pins but no routing path";
            report.mismatches.push_back(m);
            found++;
        } else {
            report.routedNetCount++;
        }
    }
}

// ============================================================
// Check 4: physical coverage
//
// For each routed net, compute the axis-aligned bounding box of
// its routePath (expanded by 2×HALF_WIDTH), then verify that
// every pin connected to that net lies inside the bbox.
//
// Using 2× margin avoids false positives from pin offset (dx,dy).
// ============================================================
void LvsEngine::checkPhysicalCoverage(Design* chip, LvsReport& report) const {
    constexpr double MARGIN = 2.0 * HALF_WIDTH;  // 20 design units = 2000 nm
    constexpr int    CAP    = 200;
    int found = 0;

    for (Net* net : chip->nets) {
        if (net->routePath.size() < 2) continue;

        // Bounding box of all route points
        double minX =  std::numeric_limits<double>::max();
        double minY =  std::numeric_limits<double>::max();
        double maxX = -std::numeric_limits<double>::max();
        double maxY = -std::numeric_limits<double>::max();

        for (const Point& p : net->routePath) {
            minX = std::min(minX, (double)p.x);
            minY = std::min(minY, (double)p.y);
            maxX = std::max(maxX, (double)p.x);
            maxY = std::max(maxY, (double)p.y);
        }

        // Expand by margin on all sides
        double bx1 = minX - MARGIN;
        double by1 = minY - MARGIN;
        double bx2 = maxX + MARGIN;
        double by2 = maxY + MARGIN;

        for (Pin* pin : net->connectedPins) {
            if (!pin->inst) continue;
            double px = pin->getAbsX();
            double py = pin->getAbsY();

            bool covered = (px >= bx1 && px <= bx2 && py >= by1 && py <= by2);
            if (!covered && found < CAP) {
                LvsMismatch m;
                m.type     = LvsMismatchType::OPEN_CIRCUIT;
                m.instName = pin->inst->name;
                m.netName  = net->name;
                m.pinName  = pin->name;
                m.message  = "pin at ("
                           + std::to_string((int)px) + ","
                           + std::to_string((int)py)
                           + ") outside routing bbox ("
                           + std::to_string((int)bx1) + ","
                           + std::to_string((int)by1) + ")-("
                           + std::to_string((int)bx2) + ","
                           + std::to_string((int)by2) + ")";
                report.mismatches.push_back(m);
                found++;
            }
        }
    }
}

// ============================================================
// Public entry point
// ============================================================
LvsReport LvsEngine::runLvs(Design* chip) {
    LvsReport report;
    report.instanceCount = (int)chip->instances.size();
    report.netCount      = (int)chip->nets.size();

    std::cout << "  [LVS] Running on " << report.instanceCount
              << " instances, " << report.netCount << " nets...\n";

    checkPlacement      (chip, report);
    checkPinConnections (chip, report);
    checkNetRouting     (chip, report);
    checkPhysicalCoverage(chip, report);

    std::cout << "  [LVS] Done — "
              << report.totalCount()          << " mismatches ("
              << report.unplacedCount()        << " unplaced, "
              << report.unconnectedPinCount()  << " floating pins, "
              << report.unroutedCount()        << " unrouted nets, "
              << report.openCircuitCount()     << " open circuits)\n";

    return report;
}
