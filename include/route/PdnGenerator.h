#pragma once
#include "db/Design.h"
#include <vector>

class PdnGenerator {
public:
    PdnGenerator(Design* design, double coreW = 400.0, double coreH = 400.0);
    void run();

private:
    void createMacroRings(Net* vdd, Net* vss);

    Design* design;
    
    // Configurable PDN Parameters
    double coreWidth;
    double coreHeight;
    double m1RowHeight = 10.0;     // M1 Logic Rails every 10um
    double m3StripeSpacing = 40.0; // M3 Vertical Mesh every 40um
    double m4StripeSpacing = 40.0; // M4 Horizontal Mesh every 40um
};
