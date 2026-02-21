#include "analysis/LogicOptimizer.h"
#include <iostream>
#include <algorithm>
#include <vector>

struct BufferInsertion {
    GateInstance* driverInst;
    Pin* driverPin;
    Pin* sinkPin;
    Net* targetNet;
    double x_loc, y_loc;
};

void LogicOptimizer::fixTiming(Design& design, Library& lib) {
    std::cout << "\n=== LOGIC OPTIMIZATION (Buffer Insertion) ===\n";

    int bufferCount = 0;
    CellDef* bufDef = lib.getCell("BUF");
    std::vector<BufferInsertion> fixesNeeded;

    // PASS 1: SCAN for violations (Do NOT modify the design here)
    for (GateInstance* inst : design.instances) {
        for (Pin* p : inst->pins) {
            if (p->isOutput && p->net) {
                Net* targetNet = p->net;

                // We only look at simple 2-pin nets for this demo
                // (Fixing multi-fanout nets requires a more complex algorithm)
                Pin* sink = nullptr;
                for (Pin* sp : targetNet->connectedPins) {
                    if (!sp->isOutput) { sink = sp; break; }
                }

                if (!sink) continue;

                double dist = std::abs(p->inst->x - sink->inst->x) + std::abs(p->inst->y - sink->inst->y);

                if (dist > 60.0) {
                    // Record the fix for later
                    BufferInsertion fix;
                    fix.driverInst = p->inst;
                    fix.driverPin = p;
                    fix.sinkPin = sink;
                    fix.targetNet = targetNet;
                    fix.x_loc = (p->inst->x + sink->inst->x) / 2;
                    fix.y_loc = (p->inst->y + sink->inst->y) / 2;
                    fixesNeeded.push_back(fix);
                }
            }
        }
    }

    // PASS 2: APPLY fixes (Safe to modify design now)
    for (const auto& fix : fixesNeeded) {
        std::cout << "  Fixing long wire on Net: " << fix.targetNet->name << "\n";

        // 1. Create Buffer GateInstance
        std::string bufName = "buf_" + std::to_string(bufferCount++);
        GateInstance* newBuf = new GateInstance(bufName, bufDef);
        newBuf->x = fix.x_loc;
        newBuf->y = fix.y_loc;
        design.addInstance(newBuf);

        // 2. Create New Net (Buffer -> Sink)
        std::string newNetName = fix.targetNet->name + "_opt";
        Net* newNet = design.createNet(newNetName);

        // 3. Connect Buffer Input to OLD net
        design.connect(newBuf, "A", fix.targetNet->name);

        // 4. Connect Buffer Output to NEW net
        design.connect(newBuf, "Y", newNetName);

        // 5. Move Sink to NEW net
        // A. Remove sink from old net's pin list
        auto& oldPins = fix.targetNet->connectedPins;
        oldPins.erase(std::remove(oldPins.begin(), oldPins.end(), fix.sinkPin), oldPins.end());

        // B. Add sink to new net
        fix.sinkPin->net = newNet;
        newNet->connectedPins.push_back(fix.sinkPin);
    }

    std::cout << "  Inserted " << bufferCount << " buffers to improve timing.\n";
}
