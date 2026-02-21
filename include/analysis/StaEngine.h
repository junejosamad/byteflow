#pragma once
#include "db/Design.h"
#include "db/Library.h"
#include <vector>
#include <algorithm>
#include <iomanip>

class StaEngine {
public:
    // The Main Loop: Updates Arrival Time (AT) for the whole chip
    void updateTiming(Design& design);

    // The Report: Prints the slowest path
    void reportTiming(Design& design);
    // Add this public function
    void checkConstraints(Design& design, double clockPeriod);

private:
    // Recursive function to compute AT for a specific pin
    double computeArrivalTime(Pin* p);
};
