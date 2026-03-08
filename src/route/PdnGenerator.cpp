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

    // 1.5. Macro Power Rings
    createMacroRings(vdd, vss);

    // 2. Draw Horizontal Logic Rails (Metal 1) - One per Row
    int numRows = (int)(coreHeight / m1RowHeight);
    
    for (int i = 0; i < numRows; ++i) {
        double y = i * m1RowHeight;
        Net* currentNet = (i % 2 == 0) ? vss : vdd; // Alternate VSS/VDD
        
        Point p1; p1.x = 0; p1.y = (int)y; p1.layer = 1; // Metal 1
        Point p2; p2.x = (int)coreWidth; p2.y = (int)y; p2.layer = 1;

        currentNet->routePath.push_back(p1);
        currentNet->routePath.push_back(p2);
    }
    std::cout << "  Generated " << numRows << " M1 Standard Cell Power Rails.\n";

    // 3. Draw Vertical Macro Stripes (Metal 3)
    int numM3Stripes = (int)(coreWidth / m3StripeSpacing);
    for (int i = 1; i <= numM3Stripes; ++i) {
        double x = i * m3StripeSpacing;
        
        // VSS Stripe (Left side of the pair)
        Point vss_p1; vss_p1.x = (int)x; vss_p1.y = 0; vss_p1.layer = 3; // Metal 3
        Point vss_p2; vss_p2.x = (int)x; vss_p2.y = (int)coreHeight; vss_p2.layer = 3;
        vss->routePath.push_back(vss_p1);
        vss->routePath.push_back(vss_p2);

        // Drop vias from M3 to M1 rows
        for (int row = 0; row < numRows; ++row) {
            if (row % 2 == 0) { // If this row is VSS
                Point via_m3; via_m3.x = (int)x; via_m3.y = (int)(row * m1RowHeight); via_m3.layer = 3;
                Point via_m1; via_m1.x = (int)x; via_m1.y = (int)(row * m1RowHeight); via_m1.layer = 1;
                vss->routePath.push_back(via_m3);
                vss->routePath.push_back(via_m1);
            }
        }

        // VDD Stripe (Offset by 5um to create a wide pair)
        double x_vdd = x + 5.0; 
        if (x_vdd < coreWidth) {
            Point vdd_p1; vdd_p1.x = (int)x_vdd; vdd_p1.y = 0; vdd_p1.layer = 3;
            Point vdd_p2; vdd_p2.x = (int)x_vdd; vdd_p2.y = (int)coreHeight; vdd_p2.layer = 3;
            vdd->routePath.push_back(vdd_p1);
            vdd->routePath.push_back(vdd_p2);

            // Drop vias from M3 to M1 rows
            for (int row = 0; row < numRows; ++row) {
                if (row % 2 != 0) { // If this row is VDD
                    Point via_m3; via_m3.x = (int)x_vdd; via_m3.y = (int)(row * m1RowHeight); via_m3.layer = 3;
                    Point via_m1; via_m1.x = (int)x_vdd; via_m1.y = (int)(row * m1RowHeight); via_m1.layer = 1;
                    vdd->routePath.push_back(via_m3);
                    vdd->routePath.push_back(via_m1);
                }
            }
        }
    }
    std::cout << "  Generated " << numM3Stripes*2 << " Vertical M3 Stripes with M3->M1 Via Arrays.\n";

    // 4. Draw Horizontal Macro Stripes (Metal 4)
    int numM4Stripes = (int)(coreHeight / m4StripeSpacing);
    for (int i = 1; i <= numM4Stripes; ++i) {
        double y = i * m4StripeSpacing;

        // VSS Stripe (Bottom side of pair)
        Point vss_p1; vss_p1.x = 0; vss_p1.y = (int)y; vss_p1.layer = 4; // Metal 4
        Point vss_p2; vss_p2.x = (int)coreWidth; vss_p2.y = (int)y; vss_p2.layer = 4;
        vss->routePath.push_back(vss_p1);
        vss->routePath.push_back(vss_p2);

        // Drop vias from M4 to M3 VSS stripes
        for (int j = 1; j <= numM3Stripes; ++j) {
            double x = j * m3StripeSpacing;
            Point via_m4; via_m4.x = (int)x; via_m4.y = (int)y; via_m4.layer = 4;
            Point via_m3; via_m3.x = (int)x; via_m3.y = (int)y; via_m3.layer = 3;
            vss->routePath.push_back(via_m4);
            vss->routePath.push_back(via_m3);
        }

        // VDD Stripe (Top side of pair, offset by 5um)
        double y_vdd = y + 5.0;
        if (y_vdd < coreHeight) {
            Point vdd_p1; vdd_p1.x = 0; vdd_p1.y = (int)y_vdd; vdd_p1.layer = 4;
            Point vdd_p2; vdd_p2.x = (int)coreWidth; vdd_p2.y = (int)y_vdd; vdd_p2.layer = 4;
            vdd->routePath.push_back(vdd_p1);
            vdd->routePath.push_back(vdd_p2);

            // Drop vias from M4 to M3 VDD stripes
            for (int j = 1; j <= numM3Stripes; ++j) {
                double x_vdd = (j * m3StripeSpacing) + 5.0;
                if (x_vdd < coreWidth) {
                    Point via_m4; via_m4.x = (int)x_vdd; via_m4.y = (int)y_vdd; via_m4.layer = 4;
                    Point via_m3; via_m3.x = (int)x_vdd; via_m3.y = (int)y_vdd; via_m3.layer = 3;
                    vdd->routePath.push_back(via_m4);
                    vdd->routePath.push_back(via_m3);
                }
            }
        }
    }
    std::cout << "  Generated " << numM4Stripes*2 << " Horizontal M4 Stripes with M4->M3 Via Arrays.\n";
}

void PdnGenerator::createMacroRings(Net* vdd, Net* vss) {
    if (!vdd || !vss) return;

    int numMacroRings = 0;
    for (GateInstance* inst : design->instances) {
        if (inst->isFixed && inst->type && inst->type->isMacro) {
            double x1 = inst->x;
            double y1 = inst->y;
            double x2 = inst->x + inst->type->width;
            double y2 = inst->y + inst->type->height;

            // VSS Inner Ring (offset 2 units)
            double vss_x1 = std::max(0.0, x1 - 2.0);
            double vss_y1 = std::max(0.0, y1 - 2.0);
            double vss_x2 = std::min(coreWidth, x2 + 2.0);
            double vss_y2 = std::min(coreHeight, y2 + 2.0);

            // Left M3
            vss->routePath.push_back({(int)vss_x1, (int)vss_y1, 3});
            vss->routePath.push_back({(int)vss_x1, (int)vss_y2, 3});
            // Right M3
            vss->routePath.push_back({(int)vss_x2, (int)vss_y1, 3});
            vss->routePath.push_back({(int)vss_x2, (int)vss_y2, 3});
            // Bottom M4
            vss->routePath.push_back({(int)vss_x1, (int)vss_y1, 4});
            vss->routePath.push_back({(int)vss_x2, (int)vss_y1, 4});
            // Top M4
            vss->routePath.push_back({(int)vss_x1, (int)vss_y2, 4});
            vss->routePath.push_back({(int)vss_x2, (int)vss_y2, 4});
            // Vias at 4 corners
            vss->routePath.push_back({(int)vss_x1, (int)vss_y1, 3}); vss->routePath.push_back({(int)vss_x1, (int)vss_y1, 4});
            vss->routePath.push_back({(int)vss_x2, (int)vss_y1, 3}); vss->routePath.push_back({(int)vss_x2, (int)vss_y1, 4});
            vss->routePath.push_back({(int)vss_x1, (int)vss_y2, 3}); vss->routePath.push_back({(int)vss_x1, (int)vss_y2, 4});
            vss->routePath.push_back({(int)vss_x2, (int)vss_y2, 3}); vss->routePath.push_back({(int)vss_x2, (int)vss_y2, 4});

            // VDD Outer Ring (offset 4 units)
            double vdd_x1 = std::max(0.0, x1 - 4.0);
            double vdd_y1 = std::max(0.0, y1 - 4.0);
            double vdd_x2 = std::min(coreWidth, x2 + 4.0);
            double vdd_y2 = std::min(coreHeight, y2 + 4.0);

            // Left M3
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y1, 3});
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y2, 3});
            // Right M3
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y1, 3});
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y2, 3});
            // Bottom M4
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y1, 4});
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y1, 4});
            // Top M4
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y2, 4});
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y2, 4});
            // Vias at 4 corners
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y1, 3}); vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y1, 4});
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y1, 3}); vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y1, 4});
            vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y2, 3}); vdd->routePath.push_back({(int)vdd_x1, (int)vdd_y2, 4});
            vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y2, 3}); vdd->routePath.push_back({(int)vdd_x2, (int)vdd_y2, 4});

            numMacroRings++;
        }
    }
    std::cout << "  Generated power rings for " << numMacroRings << " macros.\n";
}
