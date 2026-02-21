#pragma once
#include "db/Design.h"
#include "db/Library.h"

class LogicOptimizer {
public:
    // Scans the design for timing violations and inserts buffers
    void fixTiming(Design& design, Library& lib);
};
