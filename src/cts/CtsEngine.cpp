#include "CtsEngine.h"
#include "db/Design.h"
#include "util/Logger.h"
#include <iostream>
#include <algorithm>
#include <cmath>

bool CtsEngine::runCTS(Design* design, const std::string& clockNetName) {
    if (!design) return false;

    // 1. Find the clock net
    Net* clkNet = nullptr;
    for (Net* net : design->nets) {
        if (net->name == clockNetName) {
            clkNet = net;
            break;
        }
    }

    if (!clkNet) {
        Logger::info(Logger::fmt() << "CTS: no clock net '" << clockNetName
                     << "' found — skipping (combinational design)");
        return false;
    }

    // 2. Extract Sinks (Flip-Flop Clock Pins and Macro Clock Pins)
    auto extractSinks = [](Net* net) -> std::vector<Pin*> {
        std::vector<Pin*> foundSinks;
        for (Pin* pin : net->connectedPins) {
            if (!pin->isOutput && pin->inst && pin->inst->type) {
                // Determine if this is a standard cell sequential clock pin (usually 'clk', 'c', or generic)
                bool isSeqClock = pin->inst->type->isSequential;
                
                // Determine if this is a Macro clock pin (e.g. SRAM1024x32 .clk)
                bool isMacroClock = pin->inst->type->isMacro && (pin->name.find("clk") != std::string::npos || pin->name.find("ck") != std::string::npos || pin->name.find("CLK") != std::string::npos);

                if (isSeqClock || isMacroClock) {
                    foundSinks.push_back(pin);
                }
            }
        }
        return foundSinks;
    };

    std::vector<Pin*> sinks = extractSinks(clkNet);
    std::cout << "[CTS] Found " << sinks.size() << " sinks on net '" << clockNetName << "'.\n";

    // --- NEW: AUTO-DISCOVERY IF USER PROVIDED WRONG CLOCK NET ---
    if (sinks.empty()) {
        std::cout << "[CTS] Warning: 0 sinks found on '" << clockNetName << "'. Initiating auto-discovery for buffered clock tree...\n";
        
        Net* bestCandidate = nullptr;
        size_t maxSinks = 0;
        
        for (Net* checkNet : design->nets) {
            std::vector<Pin*> candidateSinks = extractSinks(checkNet);
            if (candidateSinks.size() > maxSinks) {
                maxSinks = candidateSinks.size();
                bestCandidate = checkNet;
            }
        }
        
        if (bestCandidate && maxSinks > 0) {
            std::cout << "[CTS] Auto-Discovery Success: Built tree on internal net '" << bestCandidate->name 
                      << "' which drives " << maxSinks << " sequential/macro endpoints.\n";
            clkNet = bestCandidate;
            sinks = extractSinks(clkNet);
        } else {
            std::cout << "[CTS] Auto-Discovery Failed. No clock sinks detected anywhere in the design database.\n";
            return false;
        }
    }

    // 3. Run Method of Means and Medians (MMM) to build the theoretical tree
    clockTreeRoot = buildMMM(sinks);

    std::cout << "[CTS] MMM Clock Tree built. Root branch point at: (" 
              << clockTreeRoot->x << ", " << clockTreeRoot->y << ")\n";

    // --- NEW: Physicalize the tree! ---
    std::cout << "[CTS] Legalizing branch points and inserting physical CLKBUF cells...\n";
    legalizeAndInsertBuffers(clockTreeRoot, design);
    std::cout << "[CTS] Inserted " << bufferCount << " clock buffers into the design.\n";

    std::cout << "[CTS] Weaving sub-nets...\n";
    Pin* rootBufferInput = weaveSubNets(clockTreeRoot, design);

    // The first pin in the original clkNet is the master source (e.g., the chip's input port)
    // We safely extract it before destroying the old net
    Pin* masterSource = clkNet->connectedPins[0];

    // Create the final top-level net connecting the master source to the root buffer
    Net* topNet = new Net("cts_net_top");
    topNet->connectedPins.push_back(masterSource);
    topNet->connectedPins.push_back(rootBufferInput);
    design->nets.push_back(topNet);

    // RIP out the old unroutable master clock net!
    auto it = std::remove(design->nets.begin(), design->nets.end(), clkNet);
    design->nets.erase(it, design->nets.end());
    design->netMap.erase(clkNet->name);
    delete clkNet;

    std::cout << "[CTS] Clock Tree Synthesis complete! Generated " << subnetCount + 1 << " new sub-nets.\n";

    return true;
}

std::shared_ptr<CtsNode> CtsEngine::buildMMM(std::vector<Pin*> sinks) {
    if (sinks.empty()) return nullptr;

    // Base Case 1: Single sink
    if (sinks.size() == 1) {
        Pin* p = sinks[0];
        auto node = std::make_shared<CtsNode>(p->getAbsX(), p->getAbsY());
        node->isLeaf = true;
        node->targetPin = p;
        return node;
    }

    // Base Case 2: Two sinks
    if (sinks.size() == 2) {
        Pin* p1 = sinks[0];
        Pin* p2 = sinks[1];
        
        // Find midpoint
        double mx = (p1->getAbsX() + p2->getAbsX()) / 2.0;
        double my = (p1->getAbsY() + p2->getAbsY()) / 2.0;

        auto parent = std::make_shared<CtsNode>(mx, my);
        
        auto n1 = std::make_shared<CtsNode>(p1->getAbsX(), p1->getAbsY());
        n1->isLeaf = true;
        n1->targetPin = p1;

        auto n2 = std::make_shared<CtsNode>(p2->getAbsX(), p2->getAbsY());
        n2->isLeaf = true;
        n2->targetPin = p2;

        parent->left = n1;
        parent->right = n2;
        return parent;
    }

    // 1. Find Bounding Box
    double minX = sinks[0]->getAbsX(), maxX = sinks[0]->getAbsX();
    double minY = sinks[0]->getAbsY(), maxY = sinks[0]->getAbsY();

    for (Pin* p : sinks) {
        minX = std::min(minX, p->getAbsX());
        maxX = std::max(maxX, p->getAbsX());
        minY = std::min(minY, p->getAbsY());
        maxY = std::max(maxY, p->getAbsY());
    }

    double width = maxX - minX;
    double height = maxY - minY;

    // 2. Sort Sinks along the widest axis
    if (width > height) {
        std::sort(sinks.begin(), sinks.end(), [](Pin* a, Pin* b) {
            return a->getAbsX() < b->getAbsX();
        });
    } else {
        std::sort(sinks.begin(), sinks.end(), [](Pin* a, Pin* b) {
            return a->getAbsY() < b->getAbsY();
        });
    }

    // 3. Bisect (Find Median)
    size_t median = sinks.size() / 2;
    std::vector<Pin*> leftSinks(sinks.begin(), sinks.begin() + median);
    std::vector<Pin*> rightSinks(sinks.begin() + median, sinks.end());

    // 4. Recurse
    std::shared_ptr<CtsNode> leftChild = buildMMM(leftSinks);
    std::shared_ptr<CtsNode> rightChild = buildMMM(rightSinks);

    // 5. Calculate parent branch point (Midpoint between the two children's centers of mass)
    double parentX = (leftChild->x + rightChild->x) / 2.0;
    double parentY = (leftChild->y + rightChild->y) / 2.0;

    auto parent = std::make_shared<CtsNode>(parentX, parentY);
    parent->left = leftChild;
    parent->right = rightChild;

    return parent;
}

bool CtsEngine::isLocationLegal(int x, int y, int width, int height, Design* design) {
    // Keep it inside the core boundary
    if (x < 0 || y < 0 || x + width > design->coreWidth || y + height > design->coreHeight) {
        return false;
    }

    // AABB (Axis-Aligned Bounding Box) Collision Detection
    for (auto* inst : design->instances) {
        if (!inst->type) continue;
        bool overlapX = (x < inst->x + inst->type->width) && (x + width > inst->x);
        bool overlapY = (y < inst->y + inst->type->height) && (y + height > inst->y);
        
        if (overlapX && overlapY) {
            return false; // Collision detected!
        }
    }
    return true;
}

void CtsEngine::legalizeAndInsertBuffers(std::shared_ptr<CtsNode> node, Design* design) {
    if (!node || node->isLeaf) return;

    // 1. Recurse to the bottom of the tree first (Bottom-Up Legalization)
    legalizeAndInsertBuffers(node->left, design);
    legalizeAndInsertBuffers(node->right, design);

    // 2. Define our buffer dimensions 
    CellDef* bufDef = nullptr;
    if (design->cellLibrary) {
        bufDef = design->cellLibrary->getCell("CLKBUF");
    }

    if (!bufDef) {
        std::cout << "[CTS] Error: CLKBUF CellDef not found inside cellLibrary!\n";
        return;
    }

    int bufWidth = std::max(1, (int)bufDef->width);
    int bufHeight = std::max(1, (int)bufDef->height);

    // 3. Spiral Search for the nearest empty real estate
    int searchRadius = 0;
    int stepSize = 1; // Jump by 1 grid unit to accurately find space
    bool foundLegalSpot = false;
    int bestX = node->x;
    int bestY = node->y;

    while (!foundLegalSpot && searchRadius < 1000) {
        for (int dx = -searchRadius; dx <= searchRadius; dx += stepSize) {
            for (int dy = -searchRadius; dy <= searchRadius; dy += stepSize) {
                // Only check the perimeter of the current search square
                if (std::abs(dx) != searchRadius && std::abs(dy) != searchRadius) continue;

                int testX = node->x + dx;
                int testY = node->y + dy;

                if (isLocationLegal(testX, testY, bufWidth, bufHeight, design)) {
                    bestX = testX;
                    bestY = testY;
                    foundLegalSpot = true;
                    break;
                }
            }
            if (foundLegalSpot) break;
        }
        searchRadius += stepSize;
    }

    // 4. Snap the theoretical branch point to the legal coordinate
    node->x = bestX;
    node->y = bestY;

    // 5. Dynamically inject the physical CLKBUF into the C++ database!
    GateInstance* clkBuf = new GateInstance("cts_buf_" + std::to_string(bufferCount++), bufDef);
    clkBuf->x = bestX;
    clkBuf->y = bestY;
    clkBuf->isPlaced = true;
    design->addInstance(clkBuf);
    node->bufferInst = clkBuf; // Link the physical hardware to the math node!
}

Pin* CtsEngine::weaveSubNets(std::shared_ptr<CtsNode> node, Design* design) {
    if (!node) return nullptr;

    // Base Case: If it's a leaf, just return the flip-flop's physical clock pin
    if (node->isLeaf) {
        return node->targetPin;
    }

    // Recursive Step: Get the input pins of the left and right children
    Pin* leftInputPin = weaveSubNets(node->left, design);
    Pin* rightInputPin = weaveSubNets(node->right, design);

    // Create a new sub-net driven by THIS buffer's output
    Net* subNet = new Net("cts_net_" + std::to_string(subnetCount++));
    
    // We must dynamically create the 'A' and 'Y' pins for our injected CLKBUF
    Pin* pinY = new Pin(node->bufferInst, "Y", subNet); // Source
    Pin* pinA = new Pin(node->bufferInst, "A", nullptr); // Sink (Will be connected by parent)
    pinY->isOutput = true;
    pinY->type = PinType::OUTPUT;
    pinA->isOutput = false;
    pinA->type = PinType::INPUT;
    
    node->bufferInst->pins.push_back(pinY);
    node->bufferInst->pins.push_back(pinA);

    // Source = This buffer's output (Y)
    subNet->connectedPins.push_back(pinY); 
    
    // Sinks = The inputs of the children
    if (leftInputPin) {
        subNet->connectedPins.push_back(leftInputPin);
        leftInputPin->net = subNet;
    }
    if (rightInputPin) {
        subNet->connectedPins.push_back(rightInputPin);
        rightInputPin->net = subNet;
    }

    // Add the new physical net to the database!
    design->nets.push_back(subNet);
    design->netMap[subNet->name] = subNet;

    // Return this buffer's input (A) so the parent can route to it
    return pinA; 
}
