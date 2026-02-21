#include "timer/Timer.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <iomanip>

Timer::Timer(Design* d) : design(d) {}

Timer::~Timer() {
    for (auto n : nodes) delete n;
}

// 1. Convert Netlist -> Timing Graph
void Timer::buildGraph() {
    std::cout << "  [Timer] Building Timing Graph...\n";
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
                driverNode->children.push_back(loadNode); // Connect Forward
                loadNode->parents.push_back(driverNode);  // Connect Backward
            }

            // 2. Cell Arcs (Input Pins -> Output Pin of the same Gate)
            // Note: This assumes simple logic (All Inputs affect Output)
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
}

// 2. Helper: Get Delays (Physics Placeholders)
double Timer::getGateDelay(GateInstance* inst) {
    // REAL PHYSICS: Read from the Macro definition
    if (inst->type) {
        return inst->type->intrinsicDelay;
    }
    return 10.0; // Fallback
}

double Timer::getWireDelay(Net* net) {
    if (!net || net->connectedPins.empty()) return 0.0;

    double minX = 1e9, minY = 1e9;
    double maxX = -1e9, maxY = -1e9;

    for (Pin* p : net->connectedPins) {
        if (p->inst->x < minX) minX = p->inst->x;
        if (p->inst->y < minY) minY = p->inst->y;
        if (p->inst->x > maxX) maxX = p->inst->x;
        if (p->inst->y > maxY) maxY = p->inst->y;
    }

    // HPWL = Width + Height
    double hpwl = (maxX - minX) + (maxY - minY);
    
    // Physics Constant: 0.05ns delay per unit distance
    return hpwl * 0.05; 
}

// 3. Forward Pass (Calculate Arrival Time)
void Timer::propagateArrivalTimes() {
    // Topological Sort / Levelized Traversal
    // Simple approach: Iterate until convergence (or use specific order)

    // Reset
    for (auto node : nodes) node->arrivalTime = 0;

    // We need to traverse from Inputs -> Outputs
    // A Queue-based BFS is good for this
    std::queue<TimingNode*> q;

    // Find Primary Inputs (Nodes with no parents or connected to constants)
    for (auto node : nodes) {
        if (node->parents.empty()) {
            q.push(node);
        }
    }

    while (!q.empty()) {
        TimingNode* curr = q.front();
        q.pop();

        double currentGateDelay = 0;

        // Is this an Output Pin of a gate? Add Gate Delay.
        if (curr->pin->type == PinType::OUTPUT) {
            currentGateDelay = getGateDelay(curr->pin->inst);
        }

        // Propagate to children
        for (TimingNode* child : curr->children) {
            double wireDelay = 0; 
            
            // CRITICAL FIX: If this is a Net connection (Output -> Input), calculate Wire Delay
            if (curr->pin->type == PinType::OUTPUT && child->pin->type == PinType::INPUT) {
                // They are connected by a net
                wireDelay = getWireDelay(curr->pin->net);
            }

            // Arrival at Child = Arrival at Parent + Delays
            double newArrival = curr->arrivalTime + currentGateDelay + wireDelay;

            // Maximize (Setup Time Check uses Worst Case)
            if (newArrival > child->arrivalTime) {
                child->arrivalTime = newArrival;
                q.push(child); // Since we updated, we must update its children
            }
        }
    }
}

// 4. Backward Pass (Calculate Required Time)
void Timer::propagateRequiredTimes() {
    // Set Required Time at Endpoints (e.g., 1000ps clock cycle)
    double clockCycle = 1000.0;

    // Reset
    for (auto node : nodes) node->requiredTime = std::numeric_limits<double>::infinity();

    std::queue<TimingNode*> q;

    // Find Endpoints (Nodes with no children)
    for (auto node : nodes) {
        if (node->children.empty()) {
            node->requiredTime = clockCycle;
            q.push(node);
        }
    }

    while (!q.empty()) {
        TimingNode* curr = q.front();
        q.pop();

        // Propagate Backward to Parents
        for (TimingNode* parent : curr->parents) {
            double delay = 0;
            if (parent->pin->type == PinType::OUTPUT) {
                delay = getGateDelay(parent->pin->inst); // Cell Delay
            }

            // Required at Parent = Required at Child - Delay
            double newRequired = curr->requiredTime - delay;

            // Minimize (Tightest Constraint)
            if (newRequired < parent->requiredTime) {
                parent->requiredTime = newRequired;
                q.push(parent);
            }
        }
    }
}

// 5. Calculate Slack
void Timer::calculateSlack() {
    for (auto node : nodes) {
        node->slack = node->requiredTime - node->arrivalTime;
    }
}

// Master Function
void Timer::updateTiming() {
    propagateArrivalTimes();
    propagateRequiredTimes();
    calculateSlack();
}

void Timer::reportCriticalPath() {
    std::cout << "\n=== TIMING REPORT ===\n";

    // Find worst slack
    TimingNode* worstNode = nullptr;
    double minSlack = std::numeric_limits<double>::infinity();

    for (auto node : nodes) {
        if (node->slack < minSlack) {
            minSlack = node->slack;
            worstNode = node;
        }
    }

    std::cout << "  Worst Slack: " << minSlack << " ps\n";
    if (minSlack < 0) std::cout << "  VIOLATION: Setup time failed!\n";
    else              std::cout << "  MET: Timing constraints satisfied.\n";

    if (worstNode) {
        std::cout << "  Critical Endpoint: " << worstNode->pin->inst->name << "/" << worstNode->pin->name << "\n";
    }
}
