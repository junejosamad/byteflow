#pragma once

#include <vector>
#include <memory>
#include <string>

// Forward declarations
class Design;
class GateInstance;
class Pin;
class Net;

// A node in our fractal clock tree
struct CtsNode {
    double x, y;
    bool isLeaf;          // True if this is an actual Flip-Flop pin
    Pin* targetPin;       // If leaf, points to the actual pin
    std::shared_ptr<CtsNode> left;
    std::shared_ptr<CtsNode> right;
    
    GateInstance* bufferInst; // NEW: Track the physical CLKBUF gate
    
    CtsNode(double _x, double _y) : x(_x), y(_y), isLeaf(false), targetPin(nullptr), bufferInst(nullptr) {}
};

class CtsEngine {
public:
    CtsEngine() = default;
    
    // The main entry point called from Python
    bool runCTS(Design* design, const std::string& clockNetName);

    // NEW: Expose the root for the GUI
    std::shared_ptr<CtsNode> getClockTreeRoot() const { return clockTreeRoot; }

private:
    std::shared_ptr<CtsNode> clockTreeRoot; // NEW: Store it here
    std::shared_ptr<CtsNode> buildMMM(std::vector<Pin*> sinks);
    void routeTree(std::shared_ptr<CtsNode> root, Design* design);

    // --- NEW LEGALIZER METHODS ---
    bool isLocationLegal(int x, int y, int width, int height, Design* design);
    void legalizeAndInsertBuffers(std::shared_ptr<CtsNode> node, Design* design);
    
    // Counter to give unique names to dynamically inserted buffers
    int bufferCount = 0; 
    
    // --- NEW WEAVER METHODS ---
    int subnetCount = 0;
    Pin* weaveSubNets(std::shared_ptr<CtsNode> node, Design* design);
};
