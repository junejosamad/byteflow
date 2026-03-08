#include "place/PlaceEngine.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Update Constructor
PlaceEngine::PlaceEngine(Design* d, Timer* t) : design(d), timer(t) {}

double PlaceEngine::calculateCost() {
    double totalWireLength = 0;
    
    // 1. Calculate Geometric Cost (HPWL)
    for (Net* net : design->nets) {
        if (net->connectedPins.empty()) continue;
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (Pin* p : net->connectedPins) {
            if (p->inst->x < minX) minX = p->inst->x;
            if (p->inst->y < minY) minY = p->inst->y;
            if (p->inst->x > maxX) maxX = p->inst->x;
            if (p->inst->y > maxY) maxY = p->inst->y;
        }
        totalWireLength += ((maxX - minX) + (maxY - minY));
    }

    // 2. Calculate Timing Cost
    // Only update timing periodically (controlled by caller via timingCounter)
    if (++timingCounter % 50 == 0) {
        timer->updateTiming();
    }
    
    double worstSlack = timer->getMinSlack(); 

    // Weighting Factor: How much do we care about timing vs wirelength?
    double timingWeight = 10.0; 
    
    // Cost Formula: Invert slack so "higher slack" = "lower cost"
    // We add a constant offset (e.g. 1000) to keep numbers positive
    // Simple approach: Linear penalty
    double timingPenalty = 0;
    if (worstSlack < 0) {
        timingPenalty = std::abs(worstSlack) * 100.0; // Heavy penalty for violations
    } else {
        // Even if slack is positive, we prefer higher slack.
        // Cost = (Constant - Slack) * Weight
        // Let's use the one from the prompt:
        timingPenalty = (1000.0 - worstSlack) * timingWeight;
        if (timingPenalty < 0) timingPenalty = 0; // Don't let cost go negative
    }

    // 3. Calculate Macro Overlap Penalty
    double overlapPenalty = 0;
    for (GateInstance* inst : design->instances) {
        if (inst->isFixed) continue; // Only penalize standard cells
        for (GateInstance* macro : design->instances) {
            if (!macro->isFixed || !macro->type->isMacro) continue;
            
            // Check intersection area
            double dx = std::max(0.0, std::min(inst->x + inst->type->width, macro->x + macro->type->width) - std::max(inst->x, macro->x));
            double dy = std::max(0.0, std::min(inst->y + inst->type->height, macro->y + macro->type->height) - std::max(inst->y, macro->y));
            if (dx > 0 && dy > 0) {
                overlapPenalty += (dx * dy) * 10000.0; // Huge penalty for overlapping a macro
            }
        }
    }

    return totalWireLength + timingPenalty + overlapPenalty;
}

void PlaceEngine::runPlacement(Design& design, double coreWidth, double coreHeight) {
    std::cout << "\n=== GLOBAL PLACEMENT (Simulated Annealing) ===\n";
    std::cout << "  Mode: Timing-Driven\n";

    // 1. Random Initial Placement (Skip Fixed/Macros)
    for (GateInstance* inst : design.instances) {
        if (!inst->isFixed) {
            inst->x = (double)(std::rand() % (int)coreWidth);
            inst->y = (double)(std::rand() % (int)coreHeight);
        }
    }

    // Cost logic updated
    double currentCost = calculateCost();
    std::cout << "  Initial Cost: " << currentCost << "\n";

    // 2. Annealing Loop
    // Scale iterations with design size (min 1000, max 10000)
    int numCells = (int)design.instances.size();
    int iterations = std::min(10000, std::max(1000, numCells * 10));
    double temp = 100.0;
    double coolingRate = std::pow(0.001 / 100.0, 1.0 / iterations); // Cool to 0.001 by end
    int logInterval = std::max(1, iterations / 10);

    std::cout << "  Iterations: " << iterations << " (" << numCells << " cells)\n";

    for (int i = 0; i < iterations; ++i) {
        // Pick random standard cells (skip fixed/macros)
        int idx1 = std::rand() % design.instances.size();
        int idx2 = std::rand() % design.instances.size();
        GateInstance* u = design.instances[idx1];
        GateInstance* v = design.instances[idx2];
        if (u->isFixed || v->isFixed) {
            // Keep going without changing iteration count or just accept this skip
            continue;
        }

        double ux_old = u->x, uy_old = u->y;
        double vx_old = v->x, vy_old = v->y;

        // Try Swap
        u->x = vx_old; u->y = vy_old;
        v->x = ux_old; v->y = uy_old;

        double newCost = calculateCost();
        double delta = newCost - currentCost;

        // ACCEPTANCE PROBABILITY:
        // 1. If newCost < currentCost (Better), accept immediately.
        // 2. If newCost > currentCost (Worse), accept with probability exp(-delta/T)
        bool accept = false;
        if (delta < 0) {
            accept = true;
        }
        else {
            double probability = std::exp(-delta / temp);
            if ((std::rand() / (double)RAND_MAX) < probability) {
                accept = true;
            }
        }

        if (accept) {
            currentCost = newCost;
        }
        else {
            // Revert
            u->x = ux_old; u->y = uy_old;
            v->x = vx_old; v->y = vy_old;
        }

        // Cool down
        temp *= coolingRate;

        if (i % logInterval == 0) {
            std::cout << "  Iter " << i << "/" << iterations << ": Cost = " << currentCost << " (Temp=" << temp << ")\n";
        }
    }
    std::cout << "  Final Cost:   " << currentCost << "\n";
}
