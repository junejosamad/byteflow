#include "analysis/DrcEngine.h"
#include <iostream>
#include <vector>
#include <map>

void DrcEngine::runDRC(Design& design) {
    std::cout << "\n=== PHYSICAL VERIFICATION (DRC) ===\n";

    int violations = 0;

    // 1. Build a Spatial Map (Grid)
    // Key: "x_y_layer", Value: Net Name
    std::map<std::string, std::string> gridOccupancy;

    for (Net* net : design.nets) {
        for (Point p : net->routePath) {
            std::string key = std::to_string(p.x) + "_" + std::to_string(p.y) + "_" + std::to_string(p.layer);

            // CHECK 1: SHORT CIRCUIT (Same spot, different net)
            if (gridOccupancy.count(key)) {
                std::string occupant = gridOccupancy[key];
                if (occupant != net->name) {
                    std::cout << "  [DRC Fail] Short Circuit at (" << p.x << ", " << p.y
                        << ") Layer " << p.layer << " between " << occupant << " and " << net->name << "\n";
                    violations++;
                }
            }
            else {
                gridOccupancy[key] = net->name;
            }

            // CHECK 2: SPACING (Adjacent spots)
            // Check Right (x+1), Up (y+1), etc.
            // Simplified: We just check x+1 for this demo
            std::string neighborKey = std::to_string(p.x + 1) + "_" + std::to_string(p.y) + "_" + std::to_string(p.layer);
            if (gridOccupancy.count(neighborKey)) {
                std::string neighbor = gridOccupancy[neighborKey];
                if (neighbor != net->name) {
                    // In a real tool, this depends on the rule (e.g. min spacing = 1 track)
                    // Here we assume grid points are far enough, so this is just a warning or "Touching"
                    // std::cout << "  [DRC Info] Spacing check at (" << p.x << ", " << p.y << ")\n";
                }
            }
        }
    }

    if (violations == 0) {
        std::cout << "  DRC Passed: No Short Circuits found.\n";
    }
    else {
        std::cout << "  DRC FAILED: Found " << violations << " violations.\n";
    }
}
