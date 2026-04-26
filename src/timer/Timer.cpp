#include "timer/Timer.h"
#include "db/LibertyParser.h"
#include "util/Logger.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stack>
#include <unordered_set>
#include <functional>

// ============================================================
// Constructor / Destructor
// ============================================================
Timer::Timer(Design* d, Library* lib, SpefEngine* spef)
    : design(d), library(lib), spefEngine(spef) {}

Timer::~Timer() {
    for (auto n : nodes) delete n;
    for (auto& c : corners_) if (c.ownLib && c.lib) delete c.lib;
}

// ============================================================
// 1. Graph Construction
// ============================================================
void Timer::buildGraph() {
    Logger::info("Timer: building timing graph...");

    for (auto n : nodes) delete n;
    nodes.clear();
    pinToNode.clear();
    topoOrder.clear();

    for (GateInstance* inst : design->instances) {
        for (Pin* p : inst->pins) {
            TimingNode* node = new TimingNode(p);
            nodes.push_back(node);
            pinToNode[p] = node;
        }
    }

    for (Net* net : design->nets) {
        Pin* driver = nullptr;
        std::vector<Pin*> loads;

        for (Pin* p : net->connectedPins) {
            if (p->type == PinType::OUTPUT) driver = p;
            else loads.push_back(p);
        }

        if (!driver) continue;
        TimingNode* driverNode = pinToNode[driver];

        // Net arcs: driver output → load inputs
        for (Pin* load : loads) {
            TimingNode* loadNode = pinToNode[load];
            driverNode->children.push_back(loadNode);
            loadNode->parents.push_back(driverNode);
        }

        // Cell arcs: gate inputs → gate output
        GateInstance* inst = driver->inst;
        for (Pin* p : inst->pins) {
            if (p->type == PinType::INPUT) {
                TimingNode* inputNode = pinToNode[p];
                inputNode->children.push_back(driverNode);
                driverNode->parents.push_back(inputNode);
            }
        }
    }

    int edgeCount = 0;
    for (auto n : nodes) edgeCount += (int)n->children.size();
    Logger::info(Logger::fmt() << "Timer: graph built — "
                 << nodes.size() << " nodes, " << edgeCount << " edges");

    computeTopologicalOrder();
}

// ============================================================
// 2. Topological Sort (iterative DFS, handles large graphs)
// ============================================================
void Timer::computeTopologicalOrder() {
    topoOrder.clear();
    topoOrder.reserve(nodes.size());

    std::unordered_set<TimingNode*> visited;
    std::unordered_set<TimingNode*> onStack;
    std::stack<std::pair<TimingNode*, int>> dfsStack;

    for (TimingNode* root : nodes) {
        if (visited.count(root)) continue;

        dfsStack.push({root, 0});
        onStack.insert(root);

        while (!dfsStack.empty()) {
            auto& [node, childIdx] = dfsStack.top();

            if (childIdx < (int)node->children.size()) {
                TimingNode* child = node->children[childIdx++];
                if (!visited.count(child)) {
                    // Skip back-edges (combinational loops — malformed netlist)
                    if (!onStack.count(child)) {
                        dfsStack.push({child, 0});
                        onStack.insert(child);
                    }
                }
            } else {
                visited.insert(node);
                onStack.erase(node);
                topoOrder.push_back(node);
                dfsStack.pop();
            }
        }
    }
    // DFS post-order gives reverse topo; flip to get source → sink
    std::reverse(topoOrder.begin(), topoOrder.end());
}

// ============================================================
// 3. Physics: NLDM gate delay
// ============================================================
double Timer::getNldmDelay(GateInstance* inst, const std::string& fromPin,
                            const std::string& toPin,
                            double inputSlew, double outputCap) const {
    if (!library || !inst->type) return getGateDelay(inst);

    const TimingArc* arc = inst->type->getTimingArc(fromPin, toPin);
    if (arc) {
        double d = arc->getDelay(inputSlew, outputCap);
        if (d > 0) return d;
    }
    return inst->type->getDelay(inputSlew, outputCap);
}

double Timer::getOutputSlew(GateInstance* inst, const std::string& fromPin,
                             const std::string& toPin,
                             double inputSlew, double outputCap) const {
    if (!library || !inst->type) return inputSlew;
    const TimingArc* arc = inst->type->getTimingArc(fromPin, toPin);
    if (arc) return arc->getOutputSlew(inputSlew, outputCap);
    return inputSlew;
}

double Timer::getElmoreDelay(Net* net) const {
    if (spefEngine && net) {
        double d = spefEngine->getWireDelay(net->name);
        if (d > 0) return d;
    }
    return getWireDelay(net);
}

double Timer::getNetCapacitance(Net* net) const {
    if (spefEngine && net) {
        double c = spefEngine->getNetCap(net->name);
        if (c > 0) return c;
    }
    return 5.0; // default 5 fF
}

double Timer::getGateDelay(GateInstance* inst) const {
    return inst->type ? inst->type->intrinsicDelay : 10.0;
}

double Timer::getWireDelay(Net* net) const {
    if (!net || net->connectedPins.empty()) return 0.0;
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (Pin* p : net->connectedPins) {
        if (!p->inst) continue;
        minX = std::min(minX, p->inst->x); maxX = std::max(maxX, p->inst->x);
        minY = std::min(minY, p->inst->y); maxY = std::max(maxY, p->inst->y);
    }
    return ((maxX - minX) + (maxY - minY)) * 0.05;
}

// ============================================================
// 4. Forward Pass — Arrival Times (topo order, source → sink)
// ============================================================
void Timer::propagateArrivalTimes() {
    for (auto node : nodes) {
        node->arrivalTime = 0.0;
        node->slew        = 20.0;
        node->gateDelay   = 0.0;
        node->wireDelay   = 0.0;
    }

    // Seed: primary inputs (no parents) start at inputDelay (or SDC per-port delay)
    for (auto node : nodes) {
        if (node->parents.empty()) {
            double arrival = inputDelay;
            if (design && !design->sdc.empty() && node->pin) {
                // Use per-port SDC input delay if specified
                std::string portName = node->pin->name;
                if (node->pin->inst) portName = node->pin->inst->name;
                double sdcDelay = design->sdc.inputDelay(portName, /*max=*/true);
                if (sdcDelay != 0.0) arrival = sdcDelay;
            }
            node->arrivalTime = arrival;
        }
    }
    // Sequential Q-pins: clock arrival + clk-to-Q delay
    for (auto node : nodes) {
        if (node->pin->type == PinType::OUTPUT &&
            node->pin->inst &&
            node->pin->inst->type &&
            node->pin->inst->type->isSequential) {
            node->arrivalTime = 0.0 + getGateDelay(node->pin->inst);
            node->gateDelay   = getGateDelay(node->pin->inst);
        }
    }

    for (TimingNode* curr : topoOrder) {
        // Skip sequential output pins — already seeded above
        if (curr->pin->type == PinType::OUTPUT &&
            curr->pin->inst &&
            curr->pin->inst->type &&
            curr->pin->inst->type->isSequential) {
            continue;
        }

        // Compute gate delay for combinational output pins
        if (curr->pin->type == PinType::OUTPUT && curr->pin->inst) {
            GateInstance* inst  = curr->pin->inst;
            double outputCap    = getNetCapacitance(curr->pin->net);
            double worstDelay   = 0.0;
            double worstSlew    = 20.0;
            double worstInputAT = 0.0;

            for (TimingNode* parent : curr->parents) {
                if (parent->pin->inst != inst) continue;
                if (parent->pin->type != PinType::INPUT) continue;

                double d = getNldmDelay(inst, parent->pin->name,
                                        curr->pin->name,
                                        parent->slew, outputCap);
                double s = getOutputSlew(inst, parent->pin->name,
                                         curr->pin->name,
                                         parent->slew, outputCap);
                if (parent->arrivalTime + d > worstInputAT + worstDelay) {
                    worstInputAT = parent->arrivalTime;
                    worstDelay   = d;
                    worstSlew    = s;
                }
            }
            curr->gateDelay   = worstDelay;
            curr->slew        = worstSlew;
            curr->arrivalTime = worstInputAT + worstDelay;
        }

        // Propagate to children
        for (TimingNode* child : curr->children) {
            double wireDelay = 0.0;
            // Net arc: output pin → input pin across a wire
            if (curr->pin->type == PinType::OUTPUT &&
                child->pin->type == PinType::INPUT) {
                wireDelay = getElmoreDelay(curr->pin->net);
            }

            double newAT = curr->arrivalTime + wireDelay;
            if (newAT > child->arrivalTime) {
                child->arrivalTime = newAT;
                child->wireDelay   = wireDelay;
                child->slew        = curr->slew * 1.05; // slight RC degradation
            }
        }
    }
}

// ============================================================
// 5. Backward Pass — Required Times (reverse topo order)
// ============================================================
double Timer::getSetupTime(const GateInstance* inst) const {
    if (!inst->type) return 50.0;
    for (const auto& arc : inst->type->timingArcs) {
        if (arc.setupTime > 0) return arc.setupTime;
    }
    return 50.0; // ps default
}

double Timer::getHoldTime(const GateInstance* inst) const {
    if (!inst->type) return 0.0;
    for (const auto& arc : inst->type->timingArcs) {
        if (arc.holdTime > 0) return arc.holdTime;
    }
    return 0.0;
}

bool Timer::isEndpoint(const TimingNode* n) const {
    // Primary outputs (no fanout)
    if (n->children.empty()) return true;
    // FF data inputs (setup check endpoints)
    if (n->pin->inst && n->pin->inst->type &&
        n->pin->inst->type->isSequential &&
        n->pin->name == "D") return true;
    return false;
}

void Timer::propagateRequiredTimes() {
    const double INF = std::numeric_limits<double>::infinity();

    for (auto node : nodes) node->requiredTime = INF;

    // Seed endpoints
    for (auto node : nodes) {
        if (!isEndpoint(node)) continue;

        // False path: exclude from timing check (infinite required time)
        if (design && !design->sdc.empty() && node->pin) {
            std::string portName = node->pin->name;
            if (node->pin->inst) portName = node->pin->inst->name;
            if (design->sdc.isFalsePath("", portName)) continue;
        }

        if (node->pin->inst && node->pin->inst->type &&
            node->pin->inst->type->isSequential &&
            node->pin->name == "D") {
            // Setup constraint: data must arrive before clock edge - setup - uncertainty
            double setup   = getSetupTime(node->pin->inst);
            int    mcMult  = 1;
            if (design && !design->sdc.empty() && node->pin->inst) {
                mcMult = design->sdc.multicycleMultiplier("", node->pin->inst->name);
            }
            double budget = clockPeriod * mcMult - setup - clockUncertainty + clockLatency;
            node->requiredTime = std::min(node->requiredTime, budget);
        } else {
            // Primary output: clock period - output_delay - uncertainty
            double outDel = outputDelay;
            if (design && !design->sdc.empty() && node->pin) {
                std::string portName = node->pin->name;
                if (node->pin->inst) portName = node->pin->inst->name;
                double sdcOut = design->sdc.outputDelay(portName, /*max=*/true);
                if (sdcOut != 0.0) outDel = sdcOut;
            }
            double budget = clockPeriod - outDel - clockUncertainty + clockLatency;
            node->requiredTime = std::min(node->requiredTime, budget);
        }
    }

    // Propagate in REVERSE topological order (sink → source)
    for (auto it = topoOrder.rbegin(); it != topoOrder.rend(); ++it) {
        TimingNode* curr = *it;
        if (curr->requiredTime == INF) continue;

        for (TimingNode* parent : curr->parents) {
            double delay = 0.0;

            // Cell arc: subtract the gate delay stored on the output node
            if (parent->pin->type == PinType::OUTPUT) {
                delay += parent->gateDelay;
            }
            // Net arc: subtract the wire delay stored on the load node
            if (parent->pin->type == PinType::OUTPUT &&
                curr->pin->type == PinType::INPUT) {
                delay += curr->wireDelay;
            }

            double newRequired = curr->requiredTime - delay;
            if (newRequired < parent->requiredTime) {
                parent->requiredTime = newRequired;
            }
        }
    }
}

// ============================================================
// 6. Slack Calculation — write results back to Pin objects
// ============================================================
void Timer::calculateSlack() {
    for (auto node : nodes) {
        node->slack = node->requiredTime - node->arrivalTime;
        node->pin->arrivalTime  = node->arrivalTime;
        node->pin->requiredTime = node->requiredTime;
        node->pin->slack        = node->slack;
    }
}

// ============================================================
// 6b. Hold Checks — computed after forward pass, independent of required times
//     Hold check applies only to FF D-pins.
//     Hold slack = arrivalTime - (clockLatency + holdTime + clockUncertainty)
//     Positive = hold met; negative = hold violated (path too fast).
// ============================================================
void Timer::computeHoldChecks() {
    const double INF = std::numeric_limits<double>::infinity();

    for (auto node : nodes) {
        node->holdSlack = INF;  // non-FF nodes get infinity (N/A)
    }

    for (auto node : nodes) {
        if (!node->pin || !node->pin->inst) continue;
        if (!node->pin->inst->type) continue;
        if (!node->pin->inst->type->isSequential) continue;
        if (node->pin->name != "D") continue;

        double holdTime = getHoldTime(node->pin->inst);
        // Hold required time: clock must have passed (latency) + hold window
        double holdRequired = clockLatency + holdTime + clockUncertainty;
        node->holdSlack      = node->arrivalTime - holdRequired;
        node->pin->holdSlack = node->holdSlack;  // write back for ECO engine
    }
}

// ============================================================
// 6c. Incremental STA — skip graph rebuild (Phase 3.4)
// ============================================================

// Re-run all three propagation passes without touching the graph structure.
// Use this after gate resizes: the cell type changes affect delay but not topology.
void Timer::updateTimingSkipBuild() {
    if (nodes.empty()) {
        // Graph was never built — fall back to a full build
        buildGraph();
    }
    // Apply any SDC overrides (same as updateTiming)
    if (design && !design->sdc.empty()) {
        const ClockDef* clk = design->sdc.primaryClock();
        if (clk) {
            clockPeriod      = clk->period_ps;
            clockUncertainty = clk->uncertainty_ps  > 0 ? clk->uncertainty_ps
                                                        : design->sdc.globalClockUncertainty_ps;
            clockLatency     = clk->latency_ps      > 0 ? clk->latency_ps
                                                        : design->sdc.globalClockLatency_ps;
        }
    }
    propagateArrivalTimes();
    propagateRequiredTimes();
    computeHoldChecks();
    calculateSlack();
}

// Patch the timing graph for a newly-added GateInstance (e.g. inserted buffer).
// Algorithm:
//   1. Collect all nets connected to newInst's pins (affected nets).
//   2. For each pin on those nets, clear its parent/child arc lists (remove stale arcs).
//   3. Rebuild arcs from current netlist state for those nets — same logic as buildGraph.
//   4. Recompute topological order.
void Timer::patchGraph(GateInstance* newInst) {
    if (!newInst) return;

    // --- Step 1: collect affected nets ---
    std::unordered_set<Net*> affectedNets;
    for (Pin* p : newInst->pins)
        if (p->net) affectedNets.insert(p->net);

    if (affectedNets.empty()) return;

    // Collect all pins on affected nets (includes existing + new pins)
    std::unordered_set<Pin*> affectedPins;
    for (Net* net : affectedNets)
        for (Pin* p : net->connectedPins)
            affectedPins.insert(p);
    // Also include the new instance's own pins
    for (Pin* p : newInst->pins) affectedPins.insert(p);

    // --- Step 2: create nodes for new instance's pins ---
    for (Pin* p : newInst->pins) {
        if (pinToNode.count(p) == 0) {
            TimingNode* node = new TimingNode(p);
            nodes.push_back(node);
            pinToNode[p] = node;
        }
    }

    // --- Step 3: clear arcs on affected nodes (both sides of each arc) ---
    for (Pin* p : affectedPins) {
        auto it = pinToNode.find(p);
        if (it == pinToNode.end()) continue;
        TimingNode* node = it->second;

        // Remove this node from all children's parent lists
        for (TimingNode* child : node->children)
            child->parents.erase(
                std::remove(child->parents.begin(), child->parents.end(), node),
                child->parents.end());
        // Remove this node from all parents' child lists
        for (TimingNode* parent : node->parents)
            parent->children.erase(
                std::remove(parent->children.begin(), parent->children.end(), node),
                parent->children.end());

        node->children.clear();
        node->parents.clear();
    }

    // --- Step 4: rebuild arcs for affected nets (mirrors buildGraph logic) ---
    for (Net* net : affectedNets) {
        Pin* driver = nullptr;
        std::vector<Pin*> loads;
        for (Pin* p : net->connectedPins) {
            if (p->type == PinType::OUTPUT) driver = p;
            else loads.push_back(p);
        }
        if (!driver) continue;

        auto driverIt = pinToNode.find(driver);
        if (driverIt == pinToNode.end()) continue;
        TimingNode* driverNode = driverIt->second;

        // Net arcs: driver → each load
        for (Pin* load : loads) {
            auto loadIt = pinToNode.find(load);
            if (loadIt == pinToNode.end()) continue;
            driverNode->children.push_back(loadIt->second);
            loadIt->second->parents.push_back(driverNode);
        }

        // Cell arcs: driver's gate inputs → driver output
        GateInstance* inst = driver->inst;
        for (Pin* inp : inst->pins) {
            if (inp->type != PinType::INPUT) continue;
            auto inpIt = pinToNode.find(inp);
            if (inpIt == pinToNode.end()) continue;
            inpIt->second->children.push_back(driverNode);
            driverNode->parents.push_back(inpIt->second);
        }
    }

    // --- Step 5: recompute topological order ---
    computeTopologicalOrder();
}

// ============================================================
// 7. Master Update — apply SDC constraints if loaded
// ============================================================
void Timer::updateTiming() {
    // If the design has SDC constraints, override the scalar settings.
    if (design && !design->sdc.empty()) {
        const ClockDef* clk = design->sdc.primaryClock();
        if (clk) {
            clockPeriod = clk->period_ps;
            // Clock uncertainty and latency reduce the available timing budget
            clockUncertainty = clk->uncertainty_ps > 0
                               ? clk->uncertainty_ps
                               : design->sdc.globalClockUncertainty_ps;
            clockLatency     = clk->latency_ps > 0
                               ? clk->latency_ps
                               : design->sdc.globalClockLatency_ps;
        }
    }
    propagateArrivalTimes();
    propagateRequiredTimes();
    computeHoldChecks();
    calculateSlack();
}

// ============================================================
// 8. Public Result Accessors
// ============================================================
double Timer::getWNS() const {
    double wns = std::numeric_limits<double>::infinity();
    for (auto node : nodes) {
        if (isEndpoint(node) && node->slack < wns)
            wns = node->slack;
    }
    return (wns == std::numeric_limits<double>::infinity()) ? 0.0 : wns;
}

double Timer::getTNS() const {
    double tns = 0.0;
    for (auto node : nodes) {
        if (isEndpoint(node) && node->slack < 0.0)
            tns += node->slack;
    }
    return tns;
}

int Timer::getViolationCount() const {
    int count = 0;
    for (auto node : nodes) {
        if (isEndpoint(node) && node->slack < 0.0) count++;
    }
    return count;
}

double Timer::getHoldWNS() const {
    const double INF = std::numeric_limits<double>::infinity();
    double whs = INF;
    for (auto node : nodes) {
        if (node->holdSlack < INF && node->holdSlack < whs)
            whs = node->holdSlack;
    }
    return (whs == INF) ? 0.0 : whs;
}

double Timer::getHoldTNS() const {
    double tns = 0.0;
    const double INF = std::numeric_limits<double>::infinity();
    for (auto node : nodes) {
        if (node->holdSlack < INF && node->holdSlack < 0.0)
            tns += node->holdSlack;
    }
    return tns;
}

int Timer::getHoldViolationCount() const {
    int count = 0;
    const double INF = std::numeric_limits<double>::infinity();
    for (auto node : nodes) {
        if (node->holdSlack < INF && node->holdSlack < 0.0) count++;
    }
    return count;
}

TimingSummary Timer::getSummary() const {
    TimingSummary s;
    s.wns           = getWNS();
    s.tns           = getTNS();
    s.violations    = getViolationCount();
    s.holdWns       = getHoldWNS();
    s.holdTns       = getHoldTNS();
    s.holdViolations= getHoldViolationCount();

    for (auto node : nodes) {
        if (isEndpoint(node)) s.endpoints++;
        if (node->arrivalTime > s.criticalPath)
            s.criticalPath = node->arrivalTime;
    }
    return s;
}

// ============================================================
// 9. Critical Path Report
// ============================================================
void Timer::reportCriticalPath() const {
    auto summary = getSummary();

    std::cout << "\n============================================================\n";
    std::cout << "  STATIC TIMING ANALYSIS REPORT\n";
    std::cout << "============================================================\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Clock Period : " << clockPeriod << " ps\n";
    std::cout << "  Delay Model  : "
              << (library    ? "NLDM (Non-Linear)" : "Scalar Fallback") << "\n";
    std::cout << "  Wire Model   : "
              << (spefEngine ? "Elmore RC"          : "HPWL Estimate")  << "\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  Setup WNS    : " << summary.wns << " ps\n";
    std::cout << "  Setup TNS    : " << summary.tns << " ps\n";
    std::cout << "  Setup Viols  : " << summary.violations
              << " / " << summary.endpoints << " endpoints\n";
    std::cout << "  Hold  WNS    : " << summary.holdWns << " ps\n";
    std::cout << "  Hold  TNS    : " << summary.holdTns << " ps\n";
    std::cout << "  Hold  Viols  : " << summary.holdViolations << "\n";
    bool setupOk = (summary.violations == 0);
    bool holdOk  = (summary.holdViolations == 0);
    std::cout << "  Status       : "
              << (setupOk && holdOk ? "TIMING MET"
                  : !setupOk && !holdOk ? "*** SETUP + HOLD VIOLATIONS ***"
                  : !setupOk           ? "*** SETUP VIOLATION ***"
                                       : "*** HOLD VIOLATION ***")
              << "\n";
    std::cout << "------------------------------------------------------------\n";

    // Find worst endpoint
    TimingNode* worstNode = nullptr;
    double minSlack = std::numeric_limits<double>::infinity();
    for (auto node : nodes) {
        if (isEndpoint(node) && node->slack < minSlack) {
            minSlack  = node->slack;
            worstNode = node;
        }
    }

    if (!worstNode) return;

    std::cout << "  Critical endpoint: "
              << worstNode->pin->inst->name << "/"
              << worstNode->pin->name << "\n";

    // Traceback: walk to worst-AT parent each step
    std::stack<TimingNode*> path;
    TimingNode* trace = worstNode;
    for (int depth = 0; depth < 500 && trace; depth++) {
        path.push(trace);
        TimingNode* worstParent = nullptr;
        double maxAT = -1.0;
        for (TimingNode* p : trace->parents) {
            if (p->arrivalTime > maxAT) { maxAT = p->arrivalTime; worstParent = p; }
        }
        trace = worstParent;
    }

    std::cout << "\n  Critical Path:\n";
    std::cout << "  " << std::left
              << std::setw(24) << "Instance"
              << std::setw(8)  << "Pin"
              << std::setw(10) << "CellType"
              << std::setw(12) << "Gate(ps)"
              << std::setw(12) << "Wire(ps)"
              << std::setw(14) << "Arrival(ps)"
              << std::setw(12) << "Slew(ps)"
              << "\n";
    std::cout << "  " << std::string(92, '-') << "\n";

    while (!path.empty()) {
        TimingNode* n = path.top(); path.pop();
        std::string inst  = n->pin->inst ? n->pin->inst->name : "(pi)";
        std::string type  = (n->pin->inst && n->pin->inst->type)
                            ? n->pin->inst->type->name : "?";
        std::cout << "  " << std::left
                  << std::setw(24) << inst
                  << std::setw(8)  << n->pin->name
                  << std::setw(10) << type
                  << std::setw(12) << n->gateDelay
                  << std::setw(12) << n->wireDelay
                  << std::setw(14) << n->arrivalTime
                  << std::setw(12) << n->slew
                  << "\n";
    }
    std::cout << "============================================================\n";
}

// ============================================================
// 10. All Endpoints Report
// ============================================================
void Timer::reportAllEndpoints() const {
    const double INF = std::numeric_limits<double>::infinity();

    std::cout << "\n  --- Endpoint Slack Summary ---\n";
    std::cout << "  " << std::left
              << std::setw(22) << "Instance"
              << std::setw(6)  << "Pin"
              << std::setw(14) << "Arrival(ps)"
              << std::setw(14) << "Required(ps)"
              << std::setw(14) << "SetupSlk(ps)"
              << std::setw(14) << "HoldSlk(ps)"
              << "Status\n";
    std::cout << "  " << std::string(100, '-') << "\n";

    for (auto node : nodes) {
        // show both setup endpoints and hold-check FF D-pins
        bool isSetupEP = isEndpoint(node);
        bool isHoldEP  = (node->holdSlack < INF);
        if (!isSetupEP && !isHoldEP) continue;

        std::string inst = node->pin->inst ? node->pin->inst->name : "(po)";
        bool setupOk = node->slack >= 0.0;
        bool holdOk  = (node->holdSlack >= 0.0 || node->holdSlack == INF);

        std::string status;
        if (!setupOk && !holdOk) status = "SETUP+HOLD";
        else if (!setupOk)       status = "SETUP_VIOL";
        else if (!holdOk)        status = "HOLD_VIOL";
        else                     status = "MET";

        std::cout << "  " << std::left
                  << std::setw(22) << inst
                  << std::setw(6)  << node->pin->name
                  << std::fixed << std::setprecision(2)
                  << std::setw(14) << node->arrivalTime
                  << std::setw(14) << (node->requiredTime == INF ? 0.0 : node->requiredTime)
                  << std::setw(14) << node->slack
                  << std::setw(14) << (node->holdSlack == INF ? 0.0 : node->holdSlack)
                  << status << "\n";
    }
}

// ============================================================
// 11. MCMM — Multi-Corner Multi-Mode STA  (Phase 3.1)
// ============================================================

// Register a timing corner from a Liberty file.
bool Timer::addCorner(const std::string& name, const std::string& libFile,
                      double periodPs, double uncertaintyPs, double latencyPs) {
    Library* lib = new Library();
    LibertyParser parser;
    parser.parse(libFile, *lib);
    if (lib->cells.empty()) {
        Logger::warn(Logger::fmt() << "MCMM: corner '" << name
                     << "' — no cells parsed from " << libFile);
        delete lib;
        return false;
    }
    TimingCorner c;
    c.name          = name;
    c.lib           = lib;
    c.ownLib        = true;
    c.periodPs      = periodPs;
    c.uncertaintyPs = uncertaintyPs;
    c.latencyPs     = latencyPs;
    corners_.push_back(c);
    Logger::info(Logger::fmt() << "MCMM: registered corner '" << name << "' ("
                 << lib->cells.size() << " cells"
                 << (periodPs > 0 ? ", period=" + std::to_string((int)periodPs) + "ps" : "")
                 << ")");
    return true;
}

// Run propagation for one corner, collect metrics, restore library + clock state.
CornerResult Timer::runOneCorner(const TimingCorner& corner) {
    // Save current state
    Library* savedLib         = library;
    double   savedPeriod      = clockPeriod;
    double   savedUncertainty = clockUncertainty;
    double   savedLatency     = clockLatency;

    // Apply corner library
    if (corner.lib) library = corner.lib;

    // Apply corner clock overrides; fall back to design SDC then Timer scalars
    if (corner.periodPs > 0.0) {
        clockPeriod = corner.periodPs;
        if (corner.uncertaintyPs >= 0.0) clockUncertainty = corner.uncertaintyPs;
        if (corner.latencyPs     >= 0.0) clockLatency     = corner.latencyPs;
    } else if (design && !design->sdc.empty()) {
        const ClockDef* clk = design->sdc.primaryClock();
        if (clk) {
            clockPeriod = clk->period_ps;
            if (corner.uncertaintyPs >= 0.0)
                clockUncertainty = corner.uncertaintyPs;
            else
                clockUncertainty = clk->uncertainty_ps > 0
                                   ? clk->uncertainty_ps
                                   : design->sdc.globalClockUncertainty_ps;
            if (corner.latencyPs >= 0.0)
                clockLatency = corner.latencyPs;
            else
                clockLatency = clk->latency_ps > 0
                               ? clk->latency_ps
                               : design->sdc.globalClockLatency_ps;
        }
    }

    propagateArrivalTimes();
    propagateRequiredTimes();
    computeHoldChecks();
    calculateSlack();

    CornerResult res;
    res.cornerName     = corner.name;
    res.modeName       = "functional";
    res.wns            = getWNS();
    res.tns            = getTNS();
    res.violations     = getViolationCount();
    res.holdWns        = getHoldWNS();
    res.holdTns        = getHoldTNS();
    res.holdViolations = getHoldViolationCount();
    for (auto node : nodes) if (isEndpoint(node)) res.endpoints++;

    // Restore state
    library          = savedLib;
    clockPeriod      = savedPeriod;
    clockUncertainty = savedUncertainty;
    clockLatency     = savedLatency;

    return res;
}

// Iterate all corners, run each, store results; leave graph in worst-WNS-corner state.
void Timer::runAllCorners() {
    if (corners_.empty()) {
        Logger::warn("MCMM: no corners registered — call add_corner() first");
        return;
    }
    if (nodes.empty()) buildGraph();

    cornerResults_.clear();
    Logger::info(Logger::fmt() << "MCMM: running " << corners_.size() << " corners...");

    for (const auto& corner : corners_) {
        CornerResult res = runOneCorner(corner);
        cornerResults_[corner.name] = res;
        Logger::info(Logger::fmt() << "MCMM:  corner '" << corner.name
                     << "'  WNS=" << std::fixed << std::setprecision(1) << res.wns
                     << "ps  TNS=" << res.tns << "ps  viols=" << res.violations);
    }

    // Re-run worst corner to leave pin slack values in worst-corner state for ECO
    CornerResult worst = getWorstCorner();
    for (const auto& corner : corners_) {
        if (corner.name == worst.cornerName) {
            runOneCorner(corner);
            break;
        }
    }
    Logger::info(Logger::fmt() << "MCMM: worst corner = '" << worst.cornerName
                 << "'  WNS=" << worst.wns << "ps");
}

CornerResult Timer::getCornerResult(const std::string& name) const {
    auto it = cornerResults_.find(name);
    return (it != cornerResults_.end()) ? it->second : CornerResult{};
}

std::vector<CornerResult> Timer::getAllCornerResults() const {
    std::vector<CornerResult> out;
    for (const auto& corner : corners_) {
        auto it = cornerResults_.find(corner.name);
        if (it != cornerResults_.end()) out.push_back(it->second);
    }
    return out;
}

CornerResult Timer::getWorstCorner() const {
    if (cornerResults_.empty()) return CornerResult{};
    CornerResult worst;
    worst.wns = std::numeric_limits<double>::infinity();
    for (const auto& kv : cornerResults_) {
        if (kv.second.wns < worst.wns) worst = kv.second;
    }
    return worst;
}

std::string Timer::formatMcmmReport() const {
    std::ostringstream os;
    os << "\n" << std::string(78, '=') << "\n";
    os << "  MCMM STATIC TIMING REPORT\n";
    os << std::string(78, '=') << "\n";
    os << "  " << std::left
       << std::setw(14) << "Corner"
       << std::setw(12) << "Mode"
       << std::setw(11) << "SetupWNS"
       << std::setw(11) << "SetupTNS"
       << std::setw(7)  << "Viols"
       << std::setw(11) << "HoldWNS"
       << "HoldViols\n";
    os << "  " << std::string(76, '-') << "\n";

    for (const auto& corner : corners_) {
        auto it = cornerResults_.find(corner.name);
        if (it == cornerResults_.end()) continue;
        const CornerResult& r = it->second;
        std::string status = (r.violations == 0 && r.holdViolations == 0) ? "MET"
                           : (r.violations > 0 && r.holdViolations > 0)   ? "SETUP+HOLD"
                           : (r.violations > 0)                            ? "SETUP_VIOL"
                                                                           : "HOLD_VIOL";
        os << "  " << std::left
           << std::setw(14) << r.cornerName
           << std::setw(12) << r.modeName
           << std::fixed << std::setprecision(1)
           << std::setw(11) << r.wns
           << std::setw(11) << r.tns
           << std::setw(7)  << r.violations
           << std::setw(11) << r.holdWns
           << std::setw(10) << r.holdViolations
           << status << "\n";
    }

    if (!cornerResults_.empty()) {
        CornerResult w = getWorstCorner();
        os << "  " << std::string(76, '-') << "\n";
        os << "  WORST (" << w.cornerName << ")\n";
        os << std::fixed << std::setprecision(1);
        os << "    Setup  WNS=" << w.wns << "ps"
           << "  TNS=" << w.tns << "ps"
           << "  Violations=" << w.violations << "\n";
        os << "    Hold   WNS=" << w.holdWns << "ps"
           << "  Violations=" << w.holdViolations << "\n";
    }
    os << std::string(78, '=') << "\n";
    return os.str();
}
