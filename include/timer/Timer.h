#pragma once
#include "db/Design.h"
#include "db/Library.h"
#include "analysis/SpefEngine.h"
#include <vector>
#include <map>
#include <string>
#include <limits>

// ============================================================
// PHASE 5: Enhanced Timing Node with Slew Tracking
// ============================================================
struct TimingNode {
    Pin* pin;              // Pointer to the actual Pin in OpenDB
    double arrivalTime;    // AT: When the signal gets here (from Start)
    double requiredTime;   // RAT: When the signal MUST get here (from End)
    double slack;          // Slack = RAT - AT
    double slew;           // Signal transition time at this node (ps)

    // Per-arc delay breakdown (for critical path reporting)
    double gateDelay = 0.0;   // Cell delay contribution
    double wireDelay = 0.0;   // Wire delay contribution

    // Graph Connectivity
    std::vector<TimingNode*> children; // Outgoing edges (Next pins)
    std::vector<TimingNode*> parents;  // Incoming edges (Previous pins)

    TimingNode(Pin* p) : pin(p), arrivalTime(0),
        requiredTime(std::numeric_limits<double>::infinity()),
        slack(0), slew(20.0) {} // Default 20ps input slew
};

class Timer {
public:
    // PHASE 5: Enhanced constructor with Library and SpefEngine
    Timer(Design* design, Library* lib = nullptr, SpefEngine* spef = nullptr);
    ~Timer();

    // Helper for Placer
    double getMinSlack() const {
        double minSlack = std::numeric_limits<double>::infinity();
        for (auto node : nodes) {
            if (node->slack < minSlack) minSlack = node->slack;
        }
        return minSlack;
    }

    // 1. Build the Graph from the Netlist
    void buildGraph();

    // 2. The Core STA Loop
    void updateTiming();

    // 3. Reporting
    void reportCriticalPath();

    // Set clock period for constraint checking (default 1000ps = 1GHz)
    void setClockPeriod(double period) { clockPeriod = period; }

private:
    Design* design;
    Library* library;        // PHASE 5: For NLDM lookups
    SpefEngine* spefEngine;  // PHASE 5: For Elmore wire delays
    double clockPeriod = 1000.0; // ps

    std::vector<TimingNode*> nodes;
    std::map<Pin*, TimingNode*> pinToNodeMap; // Fast lookup

    // Internal Steps
    void propagateArrivalTimes();  // Forward Pass
    void propagateRequiredTimes(); // Backward Pass
    void calculateSlack();         // Final Check

    // PHASE 5: Enhanced Physics
    double getNldmDelay(GateInstance* inst, const std::string& fromPin,
                        const std::string& toPin, double inputSlew, double outputCap);
    double getOutputSlew(GateInstance* inst, const std::string& fromPin,
                         const std::string& toPin, double inputSlew, double outputCap);
    double getElmoreDelay(Net* net);
    double getNetCapacitance(Net* net);

    // Legacy fallback
    double getGateDelay(GateInstance* inst);
    double getWireDelay(Net* net);
};
