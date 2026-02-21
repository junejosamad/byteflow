#include "analysis/PowerEngine.h"
#include <iomanip>

void PowerEngine::reportPower(Design& design, double supplyVoltage, double frequencyMHz) {
    std::cout << "\n=== POWER ANALYSIS ===\n";
    std::cout << "  Supply Voltage: " << supplyVoltage << " V\n";
    std::cout << "  Clock Freq:     " << frequencyMHz << " MHz\n";

    double totalLeakage = 0.0;
    double totalDynamic = 0.0;

    // Activity Factor (Assume signals toggle 10% of the time)
    double alpha = 0.1;

    for (GateInstance* inst : design.instances) {
        // 1. Leakage Power (Just sum it up)
        totalLeakage += inst->type->leakagePower;

        // 2. Dynamic Power (Calculate Load Capacitance)
        // We look at the Output Pin of this GateInstance
        for (Pin* p : inst->pins) {
            if (p->isOutput && p->net) {
                double netCap = 0.0;

                // Sum of input pin capacitances on this net
                for (Pin* sink : p->net->connectedPins) {
                    if (!sink->isOutput) {
                        // We need to look up the pin definition in the library to get C
                        // (Simplification: We assume a fixed C per input pin for now)
                        netCap += 0.004; // 4fF per input pin
                    }
                }

                // Add Wire Capacitance (Length * C_per_micron)
                // Assume 0.2 fF per micron of wire
                if (!p->net->routePath.empty()) {
                    netCap += p->net->routePath.size() * 0.0002;
                }

                // P_dyn = alpha * C * V^2 * f
                // Units: C is in pF, f in MHz, V in Volts -> Result in uW
                double p_dyn = alpha * netCap * (supplyVoltage * supplyVoltage) * frequencyMHz;
                totalDynamic += p_dyn;
            }
        }
    }

    // Convert to mW for display
    double leakage_mW = totalLeakage / 1000000.0; // nW to mW
    double dynamic_mW = totalDynamic / 1000.0;    // uW to mW
    double total_mW = leakage_mW + dynamic_mW;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Leakage Power: " << leakage_mW << " mW\n";
    std::cout << "  Dynamic Power: " << dynamic_mW << " mW\n";
    std::cout << "  -------------------------\n";
    std::cout << "  TOTAL POWER:   " << total_mW << " mW\n";
}
