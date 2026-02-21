#include "route/PdnGenerator.h"
#include <iostream>
#include <string>

PdnGenerator::PdnGenerator(Design* d, double coreW, double coreH) 
    : design(d), coreWidth(coreW), coreHeight(coreH) {}

void PdnGenerator::run() {
    std::cout << "=== PDN GENERATION (Power Grid) ===\n";

    // 1. Create or Find VDD/VSS Nets
    Net* vdd = nullptr;
    Net* vss = nullptr;

    // Check if they exist
    for (Net* n : design->nets) {
        if (n->name == "VDD") vdd = n;
        if (n->name == "VSS") vss = n;
    }

    // Create if missing
    if (!vdd) {
        vdd = new Net("VDD");
        design->nets.push_back(vdd);
    }
    if (!vss) {
        vss = new Net("VSS");
        design->nets.push_back(vss);
    }

    // 2. Draw Horizontal Rails (Metal 1) - One per Row
    int numRows = (int)(coreHeight / rowHeight);
    
    for (int i = 0; i < numRows; ++i) {
        double y = i * rowHeight;
        Net* currentNet = (i % 2 == 0) ? vss : vdd; // Alternate VSS/VDD
        
        // We simulate a rail by adding a 2-point path segment
        // Point 1 (Left Edge)
        Point p1; 
        p1.x = 0; 
        p1.y = (int)y; 
        p1.layer = 1; // Metal 1
        
        // Point 2 (Right Edge)
        Point p2; 
        p2.x = (int)coreWidth; 
        p2.y = (int)y; 
        p2.layer = 1;

        // Add to route path
        currentNet->routePath.push_back(p1);
        currentNet->routePath.push_back(p2);
    }
    std::cout << "  Generated " << numRows << " horizontal M1 rails.\n";

    // 3. Draw Vertical Stripes (Metal 2)
    int numStripes = (int)(coreWidth / stripeSpacing);
    
    for (int i = 1; i <= numStripes; ++i) {
        double x = i * stripeSpacing;
        
        // VSS Stripe (Left side of the pair)
        Point vss_p1; vss_p1.x = (int)x; vss_p1.y = 0; vss_p1.layer = 2; // Metal 2
        Point vss_p2; vss_p2.x = (int)x; vss_p2.y = (int)coreHeight; vss_p2.layer = 2;
        vss->routePath.push_back(vss_p1);
        vss->routePath.push_back(vss_p2);

        // VDD Stripe (Offset by half spacing)
        double x_vdd = x + (stripeSpacing / 2.0);
        if (x_vdd < coreWidth) {
            Point vdd_p1; vdd_p1.x = (int)x_vdd; vdd_p1.y = 0; vdd_p1.layer = 2;
            Point vdd_p2; vdd_p2.x = (int)x_vdd; vdd_p2.y = (int)coreHeight; vdd_p2.layer = 2;
            vdd->routePath.push_back(vdd_p1);
            vdd->routePath.push_back(vdd_p2);
        }
    }
    std::cout << "  Generated vertical M2 stripes.\n";
}
