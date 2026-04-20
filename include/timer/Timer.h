#pragma once
#include "db/Design.h"
#include "db/Library.h"
#include "analysis/SpefEngine.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <limits>
#include <functional>

// ============================================================
// Timing node: one per Pin in the netlist timing graph
// ============================================================
struct TimingNode {
    Pin* pin;
    double arrivalTime;
    double requiredTime;
    double slack;
    double slew;          // signal transition time (ps)
    double gateDelay;     // cell delay on this arc (ps)
    double wireDelay;     // net RC delay on this arc (ps)

    std::vector<TimingNode*> children;
    std::vector<TimingNode*> parents;

    TimingNode(Pin* p)
        : pin(p),
          arrivalTime(0.0),
          requiredTime(std::numeric_limits<double>::infinity()),
          slack(0.0),
          slew(20.0),
          gateDelay(0.0),
          wireDelay(0.0) {}
};

// ============================================================
// Summary returned after updateTiming()
// ============================================================
struct TimingSummary {
    double wns          = 0.0;   // Worst Negative Slack (ps)
    double tns          = 0.0;   // Total Negative Slack (ps)
    int    violations   = 0;     // Number of endpoint violations
    int    endpoints    = 0;     // Total timing endpoints checked
    double criticalPath = 0.0;   // Longest arrival time (ps)
};

// ============================================================
// Timer: full STA with NLDM, Elmore RC, topo-sorted passes
// ============================================================
class Timer {
public:
    Timer(Design* design, Library* lib = nullptr, SpefEngine* spef = nullptr);
    ~Timer();

    // --- Configuration ---
    void setClockPeriod(double period) { clockPeriod = period; }
    void setInputDelay(double delay)   { inputDelay  = delay;  }
    void setOutputDelay(double delay)  { outputDelay = delay;  }

    // --- Core flow ---
    void buildGraph();
    void updateTiming();

    // --- Results ---
    TimingSummary getSummary() const;
    double getWNS()            const;
    double getTNS()            const;
    int    getViolationCount() const;

    // Used by PlaceEngine to score timing quality during SA
    double getMinSlack() const { return getWNS(); }

    // --- Reports ---
    void reportCriticalPath() const;
    void reportAllEndpoints() const;

private:
    Design*     design;
    Library*    library;
    SpefEngine* spefEngine;

    double clockPeriod      = 1000.0;  // ps  (1 GHz default)
    double inputDelay       =    0.0;  // ps  (primary input arrival offset)
    double outputDelay      =    0.0;  // ps  (primary output budget reduction)
    double clockUncertainty =    0.0;  // ps  (set_clock_uncertainty)
    double clockLatency     =    0.0;  // ps  (set_clock_latency)

    std::vector<TimingNode*>           nodes;
    std::unordered_map<Pin*, TimingNode*> pinToNode;
    std::vector<TimingNode*>           topoOrder;  // source → sink order

    // --- Internal passes ---
    void computeTopologicalOrder();
    void propagateArrivalTimes();
    void propagateRequiredTimes();
    void calculateSlack();

    bool isEndpoint(const TimingNode* n) const;
    double getSetupTime(const GateInstance* inst) const;
    double getHoldTime (const GateInstance* inst) const;

    // --- Physics helpers ---
    double getNldmDelay(GateInstance* inst, const std::string& fromPin,
                        const std::string& toPin,
                        double inputSlew, double outputCap) const;
    double getOutputSlew(GateInstance* inst, const std::string& fromPin,
                         const std::string& toPin,
                         double inputSlew, double outputCap) const;
    double getElmoreDelay(Net* net)      const;
    double getNetCapacitance(Net* net)   const;
    double getGateDelay(GateInstance*)   const;
    double getWireDelay(Net* net)        const;
};
