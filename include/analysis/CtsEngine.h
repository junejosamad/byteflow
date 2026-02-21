#pragma once
#include "db/Design.h"
#include "db/Library.h"

class CtsEngine {
public:
    CtsEngine(Design* design) : design(design), bufferCount(0) {}
    void runCTS(Design& design, Library& lib);

private:
    Design* design;
    int bufferCount = 0;

    // Recursive H-Tree Builder
    // Returns the GateInstance* of the buffer created at this level
    GateInstance* buildTree(std::vector<GateInstance*>& sinks, int level, Library& lib);

    // Helpers
    void connect(GateInstance* driver, GateInstance* load);
    GateInstance* createBuffer(double x, double y, Library& lib);
};
