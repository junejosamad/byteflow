#include "analysis/ErcEngine.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

// ============================================================
// ErcReport counters
// ============================================================

int ErcReport::floatingInputCount() const {
    int n = 0;
    for (const auto& v : violations)
        if (v.type == ErcViolationType::FLOATING_INPUT) n++;
    return n;
}
int ErcReport::multipleDriverCount() const {
    int n = 0;
    for (const auto& v : violations)
        if (v.type == ErcViolationType::MULTIPLE_DRIVER) n++;
    return n;
}
int ErcReport::noPowerPinCount() const {
    int n = 0;
    for (const auto& v : violations)
        if (v.type == ErcViolationType::NO_POWER_PIN) n++;
    return n;
}

void ErcReport::print(int maxPrint) const {
    std::cout << "\n=== ERC REPORT ===\n";
    std::cout << "  Instances       : " << instanceCount << "\n";
    std::cout << "  Nets            : " << netCount      << "\n";
    std::cout << "  Pins            : " << pinCount      << "\n";
    std::cout << "  --- Violations ---\n";
    std::cout << "  Floating inputs : " << floatingInputCount()  << "\n";
    std::cout << "  Multiple drivers: " << multipleDriverCount() << "\n";
    std::cout << "  No power pin    : " << noPowerPinCount()     << "\n";
    std::cout << "  Total           : " << totalCount()          << "\n";

    if (violations.empty()) {
        std::cout << "  ERC PASSED — no electrical rule violations.\n";
    } else {
        std::cout << "\n  Top violations (up to " << maxPrint << "):\n";
        int shown = 0;
        for (const auto& v : violations) {
            if (shown++ >= maxPrint) break;
            const char* typeName = "?";
            switch (v.type) {
                case ErcViolationType::FLOATING_INPUT:  typeName = "FLOAT_IN";  break;
                case ErcViolationType::MULTIPLE_DRIVER: typeName = "MULTI_DRV"; break;
                case ErcViolationType::NO_POWER_PIN:    typeName = "NO_POWER";  break;
            }
            std::cout << "  [" << typeName << "]";
            if (!v.instName.empty()) std::cout << " inst=" << v.instName;
            if (!v.netName.empty())  std::cout << " net="  << v.netName;
            if (!v.pinName.empty())  std::cout << " pin="  << v.pinName;
            std::cout << "  " << v.message << "\n";
        }
        if (totalCount() > maxPrint)
            std::cout << "  ... " << (totalCount() - maxPrint) << " more\n";
    }
    std::cout << "==================\n";
}

// ============================================================
// Helper: does this net have at least one output driver?
// ============================================================
static bool netHasDriver(const Net* net) {
    for (const Pin* p : net->connectedPins)
        if (p->isOutput) return true;
    return false;
}

// ============================================================
// Check 1: floating input pins (no driver on net)
// ============================================================
void ErcEngine::checkFloatingInputs(Design* chip, ErcReport& report) const {
    int found = 0;
    for (GateInstance* inst : chip->instances) {
        for (Pin* p : inst->pins) {
            if (p->isOutput) continue;                   // only check inputs
            if (p->type == PinType::INOUT) continue;     // INOUT can self-drive
            if (p->net == nullptr) continue;             // LVS catches unconnected; skip here
            if (p->net->connectedPins.size() < 2) continue; // single-pin stub, skip
            if (!netHasDriver(p->net) && found < CAP) {
                ErcViolation v;
                v.type     = ErcViolationType::FLOATING_INPUT;
                v.instName = inst->name;
                v.netName  = p->net->name;
                v.pinName  = p->name;
                v.message  = "net '" + p->net->name + "' has no output driver";
                report.violations.push_back(v);
                found++;
            }
        }
    }
}

// ============================================================
// Check 2: multiple output drivers on same net
// ============================================================
void ErcEngine::checkMultipleDrivers(Design* chip, ErcReport& report) const {
    // Count output pins per net
    std::unordered_map<Net*, std::vector<Pin*>> driverMap;
    for (GateInstance* inst : chip->instances) {
        for (Pin* p : inst->pins) {
            if (p->isOutput && p->net != nullptr)
                driverMap[p->net].push_back(p);
        }
    }

    int found = 0;
    for (auto& [net, drivers] : driverMap) {
        if ((int)drivers.size() < 2) continue;
        if (found >= CAP) break;
        // Report once per net; list first two drivers in the message
        ErcViolation v;
        v.type    = ErcViolationType::MULTIPLE_DRIVER;
        v.netName = net->name;
        v.instName = drivers[0]->inst ? drivers[0]->inst->name : "";
        v.pinName  = drivers[0]->name;
        v.message  = std::to_string(drivers.size()) + " drivers on net '"
                   + net->name + "': e.g. "
                   + (drivers[0]->inst ? drivers[0]->inst->name : "?")
                   + "/" + drivers[0]->name + " and "
                   + (drivers[1]->inst ? drivers[1]->inst->name : "?")
                   + "/" + drivers[1]->name;
        report.violations.push_back(v);
        found++;
    }
}

// ============================================================
// Helper: is this pin name a power/ground supply pin?
// ============================================================
static bool isPowerPinName(const std::string& name) {
    // Common power/ground pin names (case-insensitive)
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    return (upper == "VDD"  || upper == "VSS"  ||
            upper == "VCC"  || upper == "GND"  ||
            upper == "VPWR" || upper == "VGND" ||
            upper == "AVDD" || upper == "AVSS");
}

// ============================================================
// Check 3: power/ground pins must have a net
// ============================================================
void ErcEngine::checkPowerPins(Design* chip, ErcReport& report) const {
    int found = 0;
    for (GateInstance* inst : chip->instances) {
        for (Pin* p : inst->pins) {
            if (!isPowerPinName(p->name)) continue;
            if (p->net != nullptr) continue;  // connected — ok
            if (found >= CAP) break;
            ErcViolation v;
            v.type     = ErcViolationType::NO_POWER_PIN;
            v.instName = inst->name;
            v.pinName  = p->name;
            v.message  = "power/ground pin '" + p->name
                       + "' on instance '" + inst->name + "' has no net";
            report.violations.push_back(v);
            found++;
        }
    }
}

// ============================================================
// Public entry point
// ============================================================
ErcReport ErcEngine::runErc(Design* chip) {
    ErcReport report;
    report.instanceCount = (int)chip->instances.size();
    report.netCount      = (int)chip->nets.size();

    // Count total pins
    for (GateInstance* inst : chip->instances)
        report.pinCount += (int)inst->pins.size();

    std::cout << "  [ERC] Running on " << report.instanceCount
              << " instances, " << report.netCount << " nets, "
              << report.pinCount << " pins...\n";

    checkFloatingInputs (chip, report);
    checkMultipleDrivers(chip, report);
    checkPowerPins      (chip, report);

    std::cout << "  [ERC] Done — "
              << report.totalCount()         << " violations ("
              << report.floatingInputCount() << " floating inputs, "
              << report.multipleDriverCount()<< " multi-driver, "
              << report.noPowerPinCount()    << " no-power-pin)\n";

    return report;
}
