#pragma once
#include "db/Design.h"
#include "timer/Timer.h"
#include <vector>
#include <cstdlib>
#include <cmath>

class PlaceEngine {
public:
    PlaceEngine(Design* design, Timer* timer);

    // Routes to SA or AnalyticalPlacer depending on design size
    void runPlacement(Design& design, double coreWidth, double coreHeight);

private:
    Design* design;
    Timer*  timer;
    int     timingCounter = 0;

    // Simulated Annealing (small designs < AnalyticalPlacer::SA_THRESHOLD)
    void runSA(Design& design, double coreWidth, double coreHeight);
    double calculateCost();
};
