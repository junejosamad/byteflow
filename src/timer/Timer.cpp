#include "timer/Timer.h"
#include "util/Logger.h"
#include <algorithm>
#include <iomanip>
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

TimingSummary Timer::getSummary() const {
    TimingSummary s;
    s.wns        = getWNS();
    s.tns        = getTNS();
    s.violations = getViolationCount();

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
    std::cout << "  WNS          : " << summary.wns << " ps\n";
    std::cout << "  TNS          : " << summary.tns << " ps\n";
    std::cout << "  Violations   : " << summary.violations
              << " / " << summary.endpoints << " endpoints\n";
    std::cout << "  Status       : "
              << (summary.violations == 0 ? "TIMING MET" : "*** TIMING VIOLATION ***")
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
    std::cout << "\n  --- Endpoint Slack Summary ---\n";
    std::cout << "  " << std::left
              << std::setw(22) << "Instance"
              << std::setw(6)  << "Pin"
              << std::setw(14) << "Arrival(ps)"
              << std::setw(14) << "Required(ps)"
              << std::setw(12) << "Slack(ps)"
              << "Status\n";
    std::cout << "  " << std::string(78, '-') << "\n";

    for (auto node : nodes) {
        if (!isEndpoint(node)) continue;
        std::string inst = node->pin->inst ? node->pin->inst->name : "(po)";
        bool ok = node->slack >= 0.0;
        std::cout << "  " << std::left
                  << std::setw(22) << inst
                  << std::setw(6)  << node->pin->name
                  << std::fixed << std::setprecision(2)
                  << std::setw(14) << node->arrivalTime
                  << std::setw(14) << node->requiredTime
                  << std::setw(12) << node->slack
                  << (ok ? "MET" : "VIOLATION") << "\n";
    }
}
