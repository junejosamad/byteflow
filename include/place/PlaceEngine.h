#include "db/Design.h"
#include "timer/Timer.h" // <--- Include Timer
#include <vector>
#include <cstdlib>
#include <cmath>

class PlaceEngine {
public:
    // Update constructor to take Timer*
    PlaceEngine(Design* design, Timer* timer);
    
    // Main function: Places gates within the given core boundary (W x H)
    void runPlacement(Design& design, double coreWidth, double coreHeight);

private:
    Design* design;
    Timer* timer;
    int timingCounter = 0;
    
    double calculateCost();
};
