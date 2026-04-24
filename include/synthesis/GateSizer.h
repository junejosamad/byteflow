#pragma once
#include "db/Design.h"
#include "timer/Timer.h"
#include <string>
#include <vector>

// ============================================================
// GateSizer — drive-strength resizing for timing and area
//
// resizeForTiming: upsize gates on the critical path to reduce WNS.
// resizeForArea  : downsize over-driven gates while preserving slack.
//
// Supports two naming conventions:
//   X2 variants  : AND2 → AND2_X2 → AND2_X4  (simple.lib style)
//   Sky130 style : sky130_fd_sc_hd__inv_2 → inv_4  (EcoEngine style)
// ============================================================

struct GateSizeResult {
    int    cellsUpsized        = 0;
    int    cellsDownsized      = 0;
    double timingImprovementPs = 0.0;  // WNS improvement (positive = better)
    double areaSavedUnits      = 0.0;  // sum of area deltas (library units)
    std::vector<std::string> resizeLog;
};

class GateSizer {
public:
    // Upsize gates on violating/critical paths.  Runs up to maxChanges swaps.
    GateSizeResult resizeForTiming(Design* chip, Timer& timer,
                                   int maxChanges = 50);

    // Downsize gates whose slack exceeds slackBudgetPs.
    // A gate is only downsized when the projected remaining slack after swap
    // still exceeds slackBudgetPs (conservative estimate: add 30% margin).
    GateSizeResult resizeForArea(Design* chip, Timer& timer,
                                 double slackBudgetPs = 100.0,
                                 int maxChanges = 50);

private:
    // Return the next-larger drive variant, or nullptr if none found.
    static CellDef* findUpsizedVariant(Library* lib, CellDef* current);

    // Return the next-smaller drive variant, or nullptr if already minimum.
    static CellDef* findDownsizedVariant(Library* lib, CellDef* current);

    // Area stored in CellDef.  Returns 0 if cell is null.
    static double cellArea(CellDef* cell);
};
