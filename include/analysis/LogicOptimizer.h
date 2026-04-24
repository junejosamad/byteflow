#pragma once
#include "db/Design.h"
#include "db/Library.h"
#include <string>
#include <vector>

// ============================================================
// LogicOptimizer — structural netlist optimization
//
// Two passes:
//   1. Dead logic removal : gates whose only output fan-out is to other
//      dead logic (no path to a primary output or FF D-pin) are removed.
//   2. Buffer chain collapsing : BUF→BUF→… chains are reduced to a
//      single hop by rewiring sinks directly to the chain's driving net.
//
// Neither pass touches sequential cells (DFF, CLKBUF, etc.).
// ============================================================

struct OptimizeResult {
    int deadGatesRemoved  = 0;
    int buffersCollapsed  = 0;  // number of redundant buffer *instances* removed
    int netsRemoved       = 0;
    std::vector<std::string> changeLog;

    bool anyChange() const {
        return deadGatesRemoved > 0 || buffersCollapsed > 0;
    }
};

class LogicOptimizer {
public:
    // Full optimization: dead-logic removal then buffer-chain collapsing.
    OptimizeResult optimize(Design* chip);

    // Individual passes (also callable standalone).
    int removeDeadLogic(Design* chip);
    int collapseBufferChains(Design* chip);

    // Legacy API kept for main.cpp compatibility.
    void fixTiming(Design& design, Library& lib);
};
