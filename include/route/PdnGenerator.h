#pragma once
#include "db/Design.h"
#include <vector>

class PdnGenerator {
public:
    PdnGenerator(Design* design, double coreW = 400.0, double coreH = 400.0);
    void run();

private:
    Design* design;
    
    // Configurable PDN Parameters
    double coreWidth;
    double coreHeight;
    double rowHeight = 10.0;     // M1 Rails every 10um
    double stripeSpacing = 20.0; // M2 Stripes every 20um
};
