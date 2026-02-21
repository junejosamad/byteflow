#include "db/Design.h"
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>

#include "db/Library.h"
#include "analysis/CtsEngine.h"

static_assert(sizeof(GateInstance) > 0, "GateInstance must be complete");

void CtsEngine::runCTS(Design& design, Library& lib) {
    std::cout << "=== CLOCK TREE SYNTHESIS (H-Tree) ===\n";

    // 1. Find the Clock Net
    Net* clockNet = nullptr;
    for (Net* n : design.nets) {
        if (n->name == "clk" || n->name == "clock" || n->name == "C") {
            clockNet = n;
            break;
        }
    }

    if (!clockNet) {
        std::cout << "  No clock net found. Skipping CTS.\n";
        return;
    }

    std::cout << "  Found Clock Net: " << clockNet->name << "\n";

    // 2. Identify Sinks (Flip-Flops connected to this net)
    std::vector<GateInstance*> sinks;
    std::vector<Pin*> keepOnOldNet;

    for (Pin* p : clockNet->connectedPins) {
        if (p->isOutput) {
            // Keep drivers (e.g. chip PIN) on the main net
            keepOnOldNet.push_back(p);
        } else {
            // Check if it is a DFF
            if (p->inst->type->isSequential) {
                sinks.push_back(p->inst);
            } else {
                // Keep non-sequential loads on the net (e.g. gates driven by clock directly?)
                keepOnOldNet.push_back(p);
            }
        }
    }
    
    // Disconnect sinks from old net
    clockNet->connectedPins = keepOnOldNet;

    if (sinks.empty()) {
        std::cout << "  No sequential sinks found. Skipping CTS.\n";
        return;
    }

    std::cout << "  Building H-Tree for " << sinks.size() << " sinks...\n";

    // 3. Build the Tree
    // The recursive function returns the "Root Buffer" of the tree
    GateInstance* rootBuffer = buildTree(sinks, 0, lib); // Pass lib

    // 4. Connect Original Clock Net to the Root Buffer Input (A)
    if (rootBuffer) {
        design.connect(rootBuffer, "A", clockNet->name);
        std::cout << "  CTS Complete. Root buffer: " << rootBuffer->name << "\n";
    }
}

GateInstance* CtsEngine::createBuffer(double x, double y, Library& lib) {
    // Create a new Buffer GateInstance
    std::string name = "clk_buf_" + std::to_string(bufferCount++);
    
    // Find BUF in library
    CellDef* bufDef = lib.getCell("CLKBUF");
    if (!bufDef) bufDef = lib.getCell("BUF"); // Fallback
    
    if (!bufDef) {
        std::cerr << "  Error: BUF/CLKBUF cell not found in library!\n";
        return nullptr;
    }

    GateInstance* buf = new GateInstance(name, bufDef);
    buf->x = x;
    buf->y = y;
    buf->isFixed = true; // Don't let the placer move it!
    buf->isPlaced = true;
    
    design->addInstance(buf);
    return buf;
}

void CtsEngine::connect(GateInstance* driver, GateInstance* load) {
    if (!driver || !load) return;

    // 1. Check if the Driver already has an Output Net
    Net* clockNet = nullptr;
    Pin* driverPin = nullptr;

    // Use existing pins if possible
    driverPin = driver->getPin("Y");
    if (!driverPin) {
       for (Pin* p : driver->pins) {
           if (p->type == PinType::OUTPUT) {
               driverPin = p;
               break;
           }
       }
    }
    
    if (driverPin) {
        clockNet = driverPin->net;
    }

    // 2. If no net exists, create a new one
    if (!clockNet) {
        std::string netName = driver->name + "_out";
        clockNet = design->createNet(netName); // Use Design::createNet for consistency

        if (driverPin) {
            driverPin->net = clockNet;
            // Avoid adding duplicates if already there (though here it was null)
            clockNet->connectedPins.push_back(driverPin);
        }
    }

    // 3. Connect the Load to this SAME net
    // Determine pin name based on type
    std::string inputPinName = "A"; // Default for Buffer
    if (load->type && load->type->isSequential) {
        inputPinName = "C"; // DFF
    }
    
    Pin* loadPin = load->getPin(inputPinName);
    // Fallbacks
    if (!loadPin) loadPin = load->getPin("C"); 
    if (!loadPin) loadPin = load->getPin("A");
    
    if (loadPin) {
        loadPin->net = clockNet;
        clockNet->connectedPins.push_back(loadPin);
    }
}

GateInstance* CtsEngine::buildTree(std::vector<GateInstance*>& currentSinks, int level, Library& lib) {
    if (currentSinks.empty()) return nullptr;

    // 1. Calculate Center of Gravity (Where the buffer should go)
    double totalX = 0, totalY = 0;
    for (auto inst : currentSinks) {
        totalX += inst->x;
        totalY += inst->y;
    }
    double centerX = totalX / currentSinks.size();
    double centerY = totalY / currentSinks.size();

    // 2. Create Buffer at Center
    GateInstance* buffer = createBuffer(centerX, centerY, lib);
    if (!buffer) return nullptr;

    // 3. Base Case: Small number of sinks -> Connect directly
    if (currentSinks.size() <= 2) { // Fanout limit = 2
        for (auto sink : currentSinks) {
            connect(buffer, sink);
        }
        return buffer;
    }

    // 4. Recursive Step: Split Sinks
    // Level 0: Split by X (Left/Right)
    // Level 1: Split by Y (Top/Bottom)
    bool splitByX = (level % 2 == 0);

    if (splitByX) {
        std::sort(currentSinks.begin(), currentSinks.end(), 
            [](GateInstance* a, GateInstance* b) { return a->x < b->x; });
    } else {
        std::sort(currentSinks.begin(), currentSinks.end(), 
            [](GateInstance* a, GateInstance* b) { return a->y < b->y; });
    }

    // Split into two groups
    size_t mid = currentSinks.size() / 2;
    std::vector<GateInstance*> leftGroup(currentSinks.begin(), currentSinks.begin() + mid);
    std::vector<GateInstance*> rightGroup(currentSinks.begin() + mid, currentSinks.end());

    // Recursively build sub-trees
    GateInstance* leftDriver = buildTree(leftGroup, level + 1, lib);
    GateInstance* rightDriver = buildTree(rightGroup, level + 1, lib);

    // Connect this buffer to the sub-drivers
    if (leftDriver) connect(buffer, leftDriver);
    if (rightDriver) connect(buffer, rightDriver);

    return buffer;
}
