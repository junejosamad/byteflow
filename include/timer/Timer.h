#pragma once
#include "db/Design.h"
#include <vector>
#include <map>
#include <string>
#include <limits>

// A Node in the Timing Graph (Wraps a Pin)
struct TimingNode {
    Pin* pin;              // Pointer to the actual Pin in OpenDB
    double arrivalTime;    // AT: When the signal gets here (from Start)
    double requiredTime;   // RAT: When the signal MUST get here (from End)
    double slack;          // Slack = RAT - AT

    // Graph Connectivity
    std::vector<TimingNode*> children; // Outgoing edges (Next pins)
    std::vector<TimingNode*> parents;  // Incoming edges (Previous pins)

    TimingNode(Pin* p) : pin(p), arrivalTime(0), requiredTime(std::numeric_limits<double>::infinity()), slack(0) {}
};

class Timer {
public:
    Timer(Design* design);
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

private:
    Design* design;
    std::vector<TimingNode*> nodes;
    std::map<Pin*, TimingNode*> pinToNodeMap; // Fast lookup

    // Internal Steps
    void propagateArrivalTimes();  // Forward Pass
    void propagateRequiredTimes(); // Backward Pass
    void calculateSlack();         // Final Check

    // Physics (Placeholder for now)
    // Physics (Placeholder for now)
    double getGateDelay(GateInstance* inst);
    double getWireDelay(Net* net);
};
