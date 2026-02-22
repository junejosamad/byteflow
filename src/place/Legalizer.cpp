#include "place/Legalizer.h"
#include <iostream>
#include <cmath>
#include <algorithm>

Legalizer::Legalizer(Design* d, double coreW, double coreH) 
    : design(d), coreWidth(coreW), coreHeight(coreH) {}

void Legalizer::run() {
    std::cout << "=== LEGALIZATION (Row Alignment) ===\n";

    // 1. Initialize Rows
    // We create horizontal strips from Y=0 to Y=CoreHeight
    int numRows = (int)(coreHeight / rowHeight);
    rows.clear();
    for (int i = 0; i < numRows; ++i) {
        Row r;
        r.y = i * rowHeight;
        r.height = rowHeight;
        r.nextFreeX = 2.0; // Start filling from the left edge (+ margin)
        rows.push_back(r);
    }
    std::cout << "  Initialized " << numRows << " rows.\n";

    // 2. Sort Cells by X Coordinate
    // This helps us pack them neatly from left to right
    std::sort(design->instances.begin(), design->instances.end(), 
        [](GateInstance* a, GateInstance* b) { return a->x < b->x; });

    // 3. Place Each Cell
    int movedCount = 0;
    for (GateInstance* inst : design->instances) {
        // Skip fixed instances (like I/O pins or pre-placed macros)
        if (inst->isFixed) continue; 
        
        // A. Find the Nearest Row (Snap Y)
        int rowIndex = std::round(inst->y / rowHeight);
        
        // Clamp to valid row indices (0 to numRows-1)
        rowIndex = std::max(0, std::min(rowIndex, numRows - 1));
        
        Row& targetRow = rows[rowIndex];

        // B. Assign Y coordinate
        inst->y = targetRow.y;

        // C. Assign X coordinate (Snap X and Resolve Overlap)
        // We cannot place to the left of 'nextFreeX' because that space is occupied
        double newX = std::max(inst->x, targetRow.nextFreeX);
        
        // Snap to manufacturing grid (siteWidth)
        newX = std::round(newX / siteWidth) * siteWidth;
        
        // --- CORE MARGIN ENFORCEMENT ---
        // Prevent cells from being placed flush against the boundary power rings
        double margin = 2.0; 
        
        if (newX < margin) {
            newX = margin; // Push away from the left VSS/VDD wall
        }
        if (newX + inst->type->width > coreWidth - margin) {
            newX = coreWidth - inst->type->width - margin; // Push away from the right wall
        }

        inst->x = newX;

        // D. Update Row's Free Space
        // Next cell must start *after* this cell's width
        targetRow.nextFreeX = inst->x + inst->type->width;
        
        movedCount++;
    }

    std::cout << "  Legalized " << movedCount << " cells.\n";
}
