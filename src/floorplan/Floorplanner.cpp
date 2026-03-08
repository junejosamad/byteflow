#include "floorplan/Floorplanner.h"
#include <iostream>
#include <cmath>

void Floorplanner::placeMacros(Design& design) {
    std::cout << "=== MACRO FLOORPLANNING ===\n";
    
    // Snap macros to the top-right edge array to leave the center open for standard cells
    double currentX = design.coreWidth;
    double currentY = design.coreHeight;
    double columnWidth = 0.0;

    int macroCount = 0;
    for (GateInstance* inst : design.instances) {
        if (inst->type && inst->type->isMacro) {
            double w = inst->type->width;
            double h = inst->type->height;
            
            // Advance to next column if it doesn't fit vertically
            if (currentY - h < 0) {
                currentY = design.coreHeight;
                currentX -= columnWidth; 
                columnWidth = 0.0;
            }

            if (w > columnWidth) {
                columnWidth = w;
            }

            inst->x = std::round(currentX - w);
            inst->y = std::round(currentY - h);
            inst->isPlaced = true;
            inst->isFixed = true; // Macros don't move during global placement or legalization
            
            currentY -= h; // Move down for the next macro
            
            std::cout << "  Placed Macro " << inst->name << " at (" << inst->x << ", " << inst->y << ") Size: " << w << "x" << h << "\n";
            macroCount++;
        }
    }
    
    if (macroCount > 0) {
        std::cout << "  Placed " << macroCount << " macros successfully.\n";
    } else {
        std::cout << "  No macros found in design. Skipping.\n";
    }
}
