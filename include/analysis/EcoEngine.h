#pragma once
#include "db/Design.h"
#include "timer/Timer.h"
#include <string>
#include <vector>

// ============================================================
// ECO (Engineering Change Order) Engine
//
// Two types of fixes:
//   Setup: gate upsizing — swap CellDef on critical-path gates to a
//          stronger drive-strength variant (no physical movement).
//   Hold:  buffer insertion — insert a delay buffer on paths that are
//          too fast and violate the FF hold window.
//
// Closure loop: alternate setup+hold passes until WNS>=0 and hold
//               violations==0, or maxIter is reached.
// ============================================================

struct EcoResult {
    int    setupFixed      = 0;    // gates resized for setup
    int    holdFixed       = 0;    // buffers inserted for hold
    int    iterations      = 0;
    double finalSetupWns   = 0.0;  // ps  (positive = met)
    double finalHoldWns    = 0.0;  // ps
    int    finalSetupViols = 0;
    int    finalHoldViols  = 0;
    // Phase 3.4 incremental STA stats
    int    fullRebuildCount   = 0;  // number of buildGraph() calls
    int    incrUpdateCount    = 0;  // number of updateTimingSkipBuild() calls
};

class EcoEngine {
public:
    // Full closure loop: run until timing is clean or maxIter exhausted.
    // Rebuilds timing graph + reruns STA after each pass.
    EcoResult runTimingClosure(Design* chip, Timer& timer, int maxIter = 10);

    // Single-pass setup fix: upsize the worst gate on each violating path.
    // Returns number of cells resized.
    int fixSetupViolations(Design* chip, Timer& timer);

    // Single-pass hold fix: insert delay buffers on hold-violating FF D-pins.
    // Returns number of buffers inserted.
    int fixHoldViolations(Design* chip, Timer& timer);

private:
    // ── Gate sizing ──────────────────────────────────────────
    // Parse drive strength from sky130 cell name (e.g., inv_2 → 2, buf_4 → 4)
    static int    getDriveStrength(const std::string& cellName);
    // Strip drive strength suffix: sky130_fd_sc_hd__inv_2 → sky130_fd_sc_hd__inv_
    static std::string getCellBase(const std::string& cellName);
    // Find next-larger drive strength cell in library
    static CellDef* findUpsizedCell(Library* lib, CellDef* current);
    // Find a buffer cell (buf_1 > buf_2 > ...) suitable for hold fixing
    static CellDef* findBufferCell(Library* lib);

    // ── Buffer insertion ─────────────────────────────────────
    // Insert a buffer in front of 'targetPin' (a FF D-pin).
    // Creates new GateInstance + two nets, places buffer near the FF.
    // Returns the new buffer instance, or nullptr on failure.
    GateInstance* insertBuffer(Design* chip, Pin* targetPin, CellDef* bufDef,
                                int& seqId);

    int ecoSeq = 0;  // counter for unique ECO instance/net names

    // Phase 3.4: track newly-inserted buffers so runTimingClosure can patch the graph
    std::vector<GateInstance*> lastInserted_;
};
