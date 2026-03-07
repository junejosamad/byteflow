#include "timer/Timer.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <iomanip>
#include <stack>

// ============================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================
Timer::Timer(Design* d, Library* lib, SpefEngine* spef)
    : design(d), library(lib), spefEngine(spef) {}

Timer::~Timer() {
    for (auto n : nodes) delete n;
}

// ============================================================
// 1. GRAPH CONSTRUCTION: Convert Netlist -> Timing Graph
// ============================================================
void Timer::buildGraph() {
    std::cout << "  [Timer] Building Timing Graph...\n";
    for (auto n : nodes) delete n;
    nodes.clear();
    pinToNodeMap.clear();

    // A. Create Nodes for every Pin
    for (GateInstance* inst : design->instances) {
        for (Pin* p : inst->pins) {
            TimingNode* node = new TimingNode(p);
            nodes.push_back(node);
            pinToNodeMap[p] = node;
        }
    }

    // B. Create Edges (The Connections)
    for (Net* net : design->nets) {
        // Find the Driver (Source) and Loads (Sinks)
        Pin* driver = nullptr;
        std::vector<Pin*> loads;

        for (Pin* p : net->connectedPins) {
            if (p->type == PinType::OUTPUT) driver = p;
            else loads.push_back(p);
        }

        if (driver) {
            TimingNode* driverNode = pinToNodeMap[driver];

            // 1. Net Arcs (Driver -> Loads)
            for (Pin* load : loads) {
                TimingNode* loadNode = pinToNodeMap[load];
                driverNode->children.push_back(loadNode);
                loadNode->parents.push_back(driverNode);
            }

            // 2. Cell Arcs (Input Pins -> Output Pin of the same Gate)
            GateInstance* inst = driver->inst;
            for (Pin* p : inst->pins) {
                if (p->type == PinType::INPUT) {
                    TimingNode* inputNode = pinToNodeMap[p];
                    inputNode->children.push_back(driverNode);
                    driverNode->parents.push_back(inputNode);
                }
            }
        }
    }

    int edgeCount = 0;
    for (auto n : nodes) edgeCount += (int)n->children.size();
    std::cout << "  [Timer] Graph: " << nodes.size() << " nodes, " << edgeCount << " edges.\n";
}

// ============================================================
// 2. PHYSICS: NLDM Delay Lookup
// ============================================================
double Timer::getNldmDelay(GateInstance* inst, const std::string& fromPin,
                            const std::string& toPin, double inputSlew, double outputCap) {
    if (!library || !inst->type) return getGateDelay(inst);

    CellDef* cell = inst->type;

    // Try NLDM arc lookup
    const TimingArc* arc = cell->getTimingArc(fromPin, toPin);
    if (arc) {
        double d = arc->getDelay(inputSlew, outputCap);
        if (d > 0) return d;
    }

    // Fallback: use CellDef::getDelay with NLDM tables
    double d = cell->getDelay(inputSlew, outputCap);
    return d;
}

double Timer::getOutputSlew(GateInstance* inst, const std::string& fromPin,
                             const std::string& toPin, double inputSlew, double outputCap) {
    if (!library || !inst->type) return inputSlew;

    const TimingArc* arc = inst->type->getTimingArc(fromPin, toPin);
    if (arc) {
        return arc->getOutputSlew(inputSlew, outputCap);
    }
    return inputSlew; // Pass-through if no NLDM data
}

// ============================================================
// 3. PHYSICS: Elmore Wire Delay from SpefEngine
// ============================================================
double Timer::getElmoreDelay(Net* net) {
    if (!spefEngine || !net) return getWireDelay(net);

    double delay = spefEngine->getWireDelay(net->name);
    if (delay > 0) return delay;

    return getWireDelay(net); // Fallback to HPWL estimate
}

double Timer::getNetCapacitance(Net* net) {
    if (!spefEngine || !net) return 5.0; // Default 5fF

    double cap = spefEngine->getNetCap(net->name);
    return (cap > 0) ? cap : 5.0;
}

// ============================================================
// LEGACY FALLBACK FUNCTIONS (used when no SPEF/NLDM available)
// ============================================================
double Timer::getGateDelay(GateInstance* inst) {
    if (inst->type) {
        return inst->type->intrinsicDelay;
    }
    return 10.0;
}

double Timer::getWireDelay(Net* net) {
    if (!net || net->connectedPins.empty()) return 0.0;

    double minX = 1e9, minY = 1e9;
    double maxX = -1e9, maxY = -1e9;

    for (Pin* p : net->connectedPins) {
        if (!p->inst) continue;
        if (p->inst->x < minX) minX = p->inst->x;
        if (p->inst->y < minY) minY = p->inst->y;
        if (p->inst->x > maxX) maxX = p->inst->x;
        if (p->inst->y > maxY) maxY = p->inst->y;
    }

    double hpwl = (maxX - minX) + (maxY - minY);
    return hpwl * 0.05;
}

// ============================================================
// 4. FORWARD PASS: Propagate Arrival Times (with NLDM + Elmore)
// ============================================================
void Timer::propagateArrivalTimes() {
    // Reset all ATs to -1 (unvisited) so the first update always triggers
    for (auto node : nodes) {
        node->arrivalTime = -1.0;
        node->slew = 20.0; // Default input slew
        node->gateDelay = 0.0;
        node->wireDelay = 0.0;
    }

    // BFS from Primary Inputs (nodes with no parents)
    std::queue<TimingNode*> q;
    for (auto node : nodes) {
        if (node->parents.empty()) {
            node->arrivalTime = 0.0; // Start nodes begin at time 0
            q.push(node);
        }
    }

    while (!q.empty()) {
        TimingNode* curr = q.front();
        q.pop();

        double currentGateDelay = 0;
        double currentSlew = curr->slew;

        // Is this an Output Pin of a gate? Compute NLDM gate delay.
        if (curr->pin->type == PinType::OUTPUT && curr->pin->inst) {
            GateInstance* inst = curr->pin->inst;

            // Find the worst-case input arrival and its slew
            double worstInputSlew = 20.0;
            for (TimingNode* parent : curr->parents) {
                if (parent->pin->inst == inst && parent->pin->type == PinType::INPUT) {
                    if (parent->slew > worstInputSlew) worstInputSlew = parent->slew;
                }
            }

            // Get downstream net capacitance for NLDM lookup
            double outputCap = 5.0;
            if (curr->pin->net) {
                outputCap = getNetCapacitance(curr->pin->net);
            }

            // NLDM delay: look up using worst input pin
            if (library && !inst->type->timingArcs.empty()) {
                // Find the worst arc (from any input pin to this output)
                double worstDelay = 0.0;
                double worstSlew = worstInputSlew;
                for (TimingNode* parent : curr->parents) {
                    if (parent->pin->inst == inst && parent->pin->type == PinType::INPUT) {
                        double d = getNldmDelay(inst, parent->pin->name, curr->pin->name,
                                               parent->slew, outputCap);
                        if (d > worstDelay) {
                            worstDelay = d;
                            worstSlew = getOutputSlew(inst, parent->pin->name, curr->pin->name,
                                                      parent->slew, outputCap);
                        }
                    }
                }
                currentGateDelay = worstDelay;
                currentSlew = worstSlew;
            } else {
                currentGateDelay = getGateDelay(inst);
            }

            curr->gateDelay = currentGateDelay;
            curr->slew = currentSlew;
        }

        // Propagate to children
        for (TimingNode* child : curr->children) {
            double wireDelay = 0;

            // Net connection (Output -> Input): add wire delay
            if (curr->pin->type == PinType::OUTPUT && child->pin->type == PinType::INPUT) {
                wireDelay = getElmoreDelay(curr->pin->net);
            }

            // Arrival at Child = Arrival at Parent + Gate Delay + Wire Delay
            double newArrival = curr->arrivalTime + currentGateDelay + wireDelay;

            if (newArrival > child->arrivalTime) {
                child->arrivalTime = newArrival;
                child->wireDelay = wireDelay;
                // Propagate slew through the wire (degrade slightly with RC)
                child->slew = currentSlew * 1.05; // Slight degradation through wire
                q.push(child);
            }
        }
    }
}

// ============================================================
// 5. BACKWARD PASS: Propagate Required Times
// ============================================================
void Timer::propagateRequiredTimes() {
    // Reset
    for (auto node : nodes) {
        node->requiredTime = std::numeric_limits<double>::infinity();
    }

    std::queue<TimingNode*> q;

    // Find Endpoints (Nodes with no children) and set their required time
    for (auto node : nodes) {
        if (node->children.empty()) {
            node->requiredTime = clockPeriod;
            q.push(node);
        }
        // Also set required time for FF data pins (setup constraint)
        if (node->pin->inst && node->pin->inst->type && node->pin->inst->type->isSequential) {
            if (node->pin->name == "D") {
                // Setup constraint: data must arrive before clock - T_setup
                double setupTime = 50.0; // default
                // Try to get setup time from timing arc
                for (const auto& arc : node->pin->inst->type->timingArcs) {
                    if (arc.setupTime > 0) {
                        setupTime = arc.setupTime;
                        break;
                    }
                }
                node->requiredTime = clockPeriod - setupTime;
                q.push(node);
            }
        }
    }

    while (!q.empty()) {
        TimingNode* curr = q.front();
        q.pop();

        for (TimingNode* parent : curr->parents) {
            double delay = 0;
            if (parent->pin->type == PinType::OUTPUT) {
                delay = parent->gateDelay; // Use the computed gate delay
            }
            // Also account for wire delay on net arcs
            if (parent->pin->type == PinType::OUTPUT && curr->pin->type == PinType::INPUT) {
                delay += curr->wireDelay;
            }

            double newRequired = curr->requiredTime - delay;

            if (newRequired < parent->requiredTime) {
                parent->requiredTime = newRequired;
                q.push(parent);
            }
        }
    }
}

// ============================================================
// 6. SLACK CALCULATION
// ============================================================
void Timer::calculateSlack() {
    for (auto node : nodes) {
        node->slack = node->requiredTime - node->arrivalTime;
        // Also write back to the Pin for other engines to read
        node->pin->arrivalTime = node->arrivalTime;
        node->pin->requiredTime = node->requiredTime;
        node->pin->slack = node->slack;
    }
}

// ============================================================
// MASTER TIMING UPDATE
// ============================================================
void Timer::updateTiming() {
    propagateArrivalTimes();
    propagateRequiredTimes();
    calculateSlack();
}

// ============================================================
// 7. ENHANCED CRITICAL PATH REPORT
// ============================================================
void Timer::reportCriticalPath() {
    std::cout << "\n============================================\n";
    std::cout << "  STATIC TIMING ANALYSIS REPORT (Phase 5)\n";
    std::cout << "============================================\n";
    std::cout << "  Clock Period: " << clockPeriod << " ps\n";
    std::cout << "  Delay Model:  " << (library ? "NLDM (Non-Linear)" : "Scalar Fallback") << "\n";
    std::cout << "  Wire Model:   " << (spefEngine ? "Elmore RC" : "HPWL Estimate") << "\n";

    if (spefEngine) {
        std::cout << "  Extracted Nets: " << spefEngine->getExtractedNetCount() << "\n";
    }

    // Find worst slack endpoint (must be an output pin or D-pin to trace backward from)
    TimingNode* worstNode = nullptr;
    double minSlack = std::numeric_limits<double>::infinity();

    for (auto node : nodes) {
        bool isEndpoint = node->children.empty();
        if (node->pin->inst && node->pin->inst->type && node->pin->inst->type->isSequential && node->pin->name == "D") {
            isEndpoint = true;
        }

        if (isEndpoint && node->slack < minSlack) {
            minSlack = node->slack;
            worstNode = node;
        }
    }

    std::cout << "\n  Worst Slack: " << std::fixed << std::setprecision(2) << minSlack << " ps\n";
    if (minSlack < 0)
        std::cout << "  STATUS: *** TIMING VIOLATION (Setup) ***\n";
    else
        std::cout << "  STATUS: TIMING MET\n";

    if (worstNode) {
        std::cout << "  Critical Endpoint: " << worstNode->pin->inst->name
                  << "/" << worstNode->pin->name << "\n";
    }

    // ---- CRITICAL PATH TRACEBACK ----
    // Walk backward from the worst endpoint to find the full critical path
    std::cout << "\n  --- Critical Path Breakdown ---\n";
    std::cout << "  " << std::left
              << std::setw(22) << "Instance"
              << std::setw(8)  << "Pin"
              << std::setw(8)  << "Type"
              << std::setw(14) << "Gate (ps)"
              << std::setw(14) << "Wire (ps)"
              << std::setw(14) << "Arrival (ps)"
              << std::setw(14) << "Slew (ps)"
              << "\n";
    std::cout << "  " << std::string(96, '-') << "\n";

    if (worstNode) {
        // Trace the critical path backward
        std::stack<TimingNode*> pathStack;
        TimingNode* trace = worstNode;

        while (trace) {
            pathStack.push(trace);
            // Find the parent with the worst arrival time
            TimingNode* worstParent = nullptr;
            double maxAT = -1.0;
            for (TimingNode* p : trace->parents) {
                if (p->arrivalTime > maxAT) {
                    maxAT = p->arrivalTime;
                    worstParent = p;
                }
            }
            trace = worstParent;

            // Safety: avoid infinite loops
            if (pathStack.size() > 200) break;
        }

        // Print the path from start to end
        int hop = 0;
        while (!pathStack.empty()) {
            TimingNode* n = pathStack.top();
            pathStack.pop();

            std::string instName = n->pin->inst ? n->pin->inst->name : "?";
            std::string typeName = (n->pin->inst && n->pin->inst->type) ? n->pin->inst->type->name : "?";
            std::string pinName = n->pin->name;

            std::cout << "  " << std::left
                      << std::setw(22) << instName
                      << std::setw(8)  << pinName
                      << std::setw(8)  << typeName
                      << std::fixed << std::setprecision(2)
                      << std::setw(14) << n->gateDelay
                      << std::setw(14) << n->wireDelay
                      << std::setw(14) << n->arrivalTime
                      << std::setw(14) << n->slew
                      << "\n";
            hop++;
        }
        std::cout << "  " << std::string(96, '-') << "\n";
        std::cout << "  Path depth: " << hop << " nodes\n";
    }

    // ---- SETUP/HOLD CONSTRAINT SUMMARY ----
    std::cout << "\n  --- Timing Constraints (Flip-Flop Endpoints) ---\n";
    int violations = 0;
    int met = 0;

    for (auto node : nodes) {
        if (!node->pin->inst || !node->pin->inst->type) continue;
        if (!node->pin->inst->type->isSequential) continue;
        if (node->pin->name != "D") continue;

        double slack = node->slack;
        std::string status = (slack < 0) ? "VIOLATION" : "MET";
        if (slack < 0) violations++;
        else met++;

        std::cout << "  " << std::left << std::setw(20) << node->pin->inst->name
                  << " D-pin: AT=" << std::fixed << std::setprecision(2) << node->arrivalTime
                  << "ps  RT=" << node->requiredTime
                  << "ps  Slack=" << slack << "ps  [" << status << "]\n";
    }

    std::cout << "\n  Setup Violations: " << violations << "\n";
    std::cout << "  Setup Met: " << met << "\n";
    std::cout << "============================================\n";
}
