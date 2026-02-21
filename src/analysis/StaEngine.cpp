#include "analysis/StaEngine.h"
#include <iostream>

// Main Function: Run the timer
void StaEngine::updateTiming(Design& design) {
    std::cout << "Running Static Timing Analysis...\n";

    // 1. Reset all timing values
    for (GateInstance* inst : design.instances) {
        for (Pin* p : inst->pins) {
            p->arrivalTime = -1.0;
        }
    }

    // 2. Compute times for all Logic Outputs and Primary Outputs
    for (GateInstance* inst : design.instances) {
        for (Pin* p : inst->pins) {
            if (p->isOutput) {
                computeArrivalTime(p);
            }
        }
    }

    // 3. FORCE CALCULATION for Flip-Flop Input Pins (Endpoints)
    //    These pins are "sinks" - nothing asks for their time, so we must ask manually.
    for (GateInstance* inst : design.instances) {
        if (inst->type->isSequential) {
            // Find the Data pin (usually named "D")
            Pin* d_pin = inst->getPin("D");
            if (d_pin) {
                computeArrivalTime(d_pin); // <--- THE FIX
            }
        }
    }
}

// Recursive Solver (The Math)
double StaEngine::computeArrivalTime(Pin* p) {
    // 1. MEMOIZATION: If already computed, return it (Avoid re-work)
    if (p->arrivalTime >= 0.0) return p->arrivalTime;

    // 2. BASE CASE: If this is an Input Pin of the chip, AT = 0
    if (p->net && p->net->connectedPins.size() == 0) {
        // (Simplified: real parser handles ports better)
        return 0.0;
    }

    // 3. IF OUTPUT PIN: Delay = Max(Input Pins) + Gate Delay
    // 3. IF OUTPUT PIN
    if (p->isOutput) {
        // --- NEW LOGIC START ---
        // If this is a Flip-Flop (DFF), the signal starts here!
        if (p->inst->type->isSequential) {
            // For now, assume Clock arrives at 0.0
            // AT = Clock Delay (0) + Clk-to-Q Delay
            p->arrivalTime = 0.0 + p->inst->type->delay;
            return p->arrivalTime;
        }
        // --- NEW LOGIC END ---
        double maxInputAT = 0.0;

        // Look at all input pins of this SAME gate
        for (Pin* neighbor : p->inst->pins) {
            if (!neighbor->isOutput) {
                // Recursive Call!
                double inputAT = computeArrivalTime(neighbor);
                if (inputAT > maxInputAT) maxInputAT = inputAT;
            }
        }

        // Result = Worst Input Time + Gate Delay
        p->arrivalTime = maxInputAT + p->inst->type->delay;
        return p->arrivalTime;
    }

    // 4. IF INPUT PIN: Delay = Arrival Time of the Driver Pin on the Net
    else {
        // Find who is driving this net
        if (p->net) {
            for (Pin* driver : p->net->connectedPins) {
                if (driver->isOutput) {
                    // Recursive Call to the previous gate!
                    p->arrivalTime = computeArrivalTime(driver);
                    return p->arrivalTime;
                }
            }
        }
        return 0.0; // Floating input
    }
}

void StaEngine::reportTiming(Design& design) {
    std::cout << "\n=== TIMING REPORT ===\n";
    std::cout << std::left << std::setw(20) << "GateInstance"
        << std::setw(10) << "Type"
        << std::setw(15) << "Arrival Time" << "\n";
    std::cout << "---------------------------------------------\n";

    double worstDelay = 0.0;

    for (GateInstance* inst : design.instances) {
        // Find the output pin of this GateInstance
        for (Pin* p : inst->pins) {
            if (p->isOutput) {
                std::cout << std::left << std::setw(20) << inst->name
                    << std::setw(10) << inst->type->name
                    << std::fixed << std::setprecision(2) << p->arrivalTime << " ns\n";

                if (p->arrivalTime > worstDelay) worstDelay = p->arrivalTime;
            }
        }
    }
    std::cout << "---------------------------------------------\n";
    std::cout << "CRITICAL PATH DELAY: " << worstDelay << " ns\n";
}

void StaEngine::checkConstraints(Design& design, double clockPeriod) {
    std::cout << "\n=== TIMING CONSTRAINTS (Setup Check) ===\n";
    std::cout << "Clock Period: " << clockPeriod << " ns\n";

    // T_setup (Hardcoded for now, usually in Library)
    double T_setup = 0.5;

    for (GateInstance* inst : design.instances) {
        if (inst->type->isSequential) {
            // Check the 'D' pin
            Pin* d_pin = inst->getPin("D");
            if (d_pin) {
                // Calculate Required Time: Clock - Setup
                double required = clockPeriod - T_setup;
                double slack = required - d_pin->arrivalTime;

                std::cout << "  GateInstance " << inst->name << " (D Pin):\n";
                std::cout << "    Arrival:  " << d_pin->arrivalTime << " ns\n";
                std::cout << "    Required: " << required << " ns\n";
                std::cout << "    Slack:    " << slack << " ns";

                if (slack < 0) std::cout << " (VIOLATION) <<<< FAIL";
                else           std::cout << " (MET)";
                std::cout << "\n";
            }
        }
    }
}
