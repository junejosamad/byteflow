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
    double slack;         // setup slack = requiredTime - arrivalTime
    double holdSlack;     // hold slack  = arrivalTime - holdRequired (FF D-pins only)
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
          holdSlack(std::numeric_limits<double>::infinity()),
          slew(20.0),
          gateDelay(0.0),
          wireDelay(0.0) {}
};

// ============================================================
// Summary returned after updateTiming()
// ============================================================
struct TimingSummary {
    double wns            = 0.0;  // Setup WNS (ps)
    double tns            = 0.0;  // Setup TNS (ps)
    int    violations     = 0;    // Setup violation count
    int    endpoints      = 0;    // Total timing endpoints checked
    double criticalPath   = 0.0;  // Longest arrival time (ps)

    double holdWns        = 0.0;  // Hold WNS (ps)  — positive = met
    double holdTns        = 0.0;  // Hold TNS (ps)  — sum of negative hold slacks
    int    holdViolations = 0;    // Hold violation count (hold slack < 0)
};

// ============================================================
// Timer: full STA with NLDM, Elmore RC, topo-sorted passes
// ============================================================
class TimingReporter;  // forward declaration for friend

class Timer {
    friend class TimingReporter;
public:
    Timer(Design* design, Library* lib = nullptr, SpefEngine* spef = nullptr);
    ~Timer();

    // --- Configuration ---
    void setClockPeriod(double period)      { clockPeriod      = period; }
    void setInputDelay(double delay)        { inputDelay       = delay;  }
    void setOutputDelay(double delay)       { outputDelay      = delay;  }
    void setClockUncertainty(double unc)    { clockUncertainty = unc;    }
    void setClockLatency(double lat)        { clockLatency     = lat;    }

    // --- Core flow ---
    void buildGraph();
    void updateTiming();

    // --- Results (setup) ---
    TimingSummary getSummary()      const;
    double getWNS()                 const;
    double getTNS()                 const;
    int    getViolationCount()      const;

    // --- Results (hold) ---
    double getHoldWNS()             const;
    double getHoldTNS()             const;
    int    getHoldViolationCount()  const;

    // Used by PlaceEngine to score timing quality during SA
    double getMinSlack() const { return getWNS(); }

    // --- Reports ---
    void reportCriticalPath()  const;
    void reportAllEndpoints()  const;  // shows both setup and hold columns

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
    void computeHoldChecks();   // hold slack = arrival - (latency + holdTime + uncertainty)
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
