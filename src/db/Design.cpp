#include "db/Design.h"

GateInstance::GateInstance(std::string n, CellDef* t) : name(n), type(t) {
    if (!t) return; // Safety check
    
    for (const auto& pDef : t->pins) {
        Pin* p = new Pin(this, pDef.name, nullptr); // Use specific constructor
        p->isOutput = pDef.isOutput;
        // Set PinType based on isOutput
        p->type = pDef.isOutput ? PinType::OUTPUT : PinType::INPUT;

        // COPY OFFSETS
        p->dx = pDef.dx;
        p->dy = pDef.dy;

        pins.push_back(p);
    }
}
