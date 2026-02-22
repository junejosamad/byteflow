#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <map>
#include "Library.h"

#ifdef GateInstance
#undef GateInstance
#endif

// Forward declarations
class Net;
class GateInstance;


// Add this at the top
struct Point {
    int x, y;
    int layer = 1; // 1 = Metal1 (Blue), 2 = Metal2 (Red)

};

// --- NEW: Pin Direction Enum ---
enum class PinType {
    INPUT,
    OUTPUT,
    INOUT
};

struct MacroPin {
    std::string name;
    std::string direction; // "INPUT" or "OUTPUT"
    PinType dir = PinType::INPUT;
    // Pin center offset from cell origin (from LEF RECT geometry)
    double offsetX = 0.0;
    double offsetY = 0.0;
};

struct Macro {
    std::string name;
    double width = 0.0;
    double height = 0.0;
    std::map<std::string, MacroPin> pins; // Keyed by pin name

    Macro() = default;
    Macro(const std::string& n) : name(n) {}
};


// A Pin is where a Wire meets a Gate
struct Pin {
    std::string name;       // "A", "Y"
    GateInstance* inst;         // The gate this pin belongs to
    Net* net;               // The wire connected to this pin
    bool isOutput;
	PinType type;          // NEW: Pin direction (INPUT, OUTPUT, INOUT)

    // NEW: Store physical offset (relative to cell bottom-left origin)
    double dx = 0.0;
    double dy = 0.0;
    
    // Absolute coordinates (defined below after GateInstance)
    double getAbsX() const;
    double getAbsY() const;
    
    // TIMING DATA (Populated by STA Engine)
    double arrivalTime = 0.0;
    double requiredTime = 0.0;
    double slack = 0.0;

    // Constructor defaults to INPUT, we fix this in the Parser
    Pin(GateInstance* i, std::string n, Net* w)
        : inst(i), name(n), net(w), type(PinType::INPUT) {
    }
};

// A Net is a Wire
// Update the Net class
class Net {
public:
    std::string name;
    std::vector<Pin*> connectedPins;

    // NEW: The physical path (List of grid coordinates)
    std::vector<Point> routePath;

    Net(std::string n) : name(n) {}
};

// An GateInstance is a Gate (e.g., "u1")
class GateInstance {
public:
    std::string name;
    // Macro* type;          // Remove conflicting/unused pointer
    CellDef* type;          // Pointer to Library definition (NAND2)
    std::vector<Pin*> pins;
    
    // PHYSICAL DATA (Populated by Placer)
    double x = 0.0;
    double y = 0.0;
    bool isPlaced = false;
    bool isFixed = false;

    GateInstance(std::string n, CellDef* t);
    
    Pin* getPin(std::string pinName) {
        for (Pin* p : pins) {
            if (p->name == pinName) return p;
        }
        return nullptr;
    }
};

inline double Pin::getAbsX() const { return inst ? inst->x + dx : dx; }
inline double Pin::getAbsY() const { return inst ? inst->y + dy : dy; }

// The Container for the whole chip
class Design {
public:
    std::string name;
    std::vector<GateInstance*> instances;
    std::vector<Net*> nets;
    std::unordered_map<std::string, Net*> netMap;
    std::map<std::string, Macro*> library; // Map of macro name to Macro definition

    double coreWidth = 0.0;
    double coreHeight = 0.0;

    void addInstance(GateInstance* inst) {
        instances.push_back(inst);
    }

    Net* createNet(std::string name) {
        if (netMap.find(name) == netMap.end()) {
            Net* n = new Net(name);
            nets.push_back(n);
            netMap[name] = n;
        }
        return netMap[name];
    }
    
    // The "Linker" function
    void connect(GateInstance* inst, std::string pinName, std::string netName) {
        Pin* p = inst->getPin(pinName);
        Net* n = createNet(netName);
        
        if (p) {
            p->net = n;
            n->connectedPins.push_back(p);
        }
    }
};

