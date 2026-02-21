#pragma once
#include "db/Design.h"
#include <vector>

struct Row {
    double y;           // Y-coordinate of the bottom of the row
    double height;      // Standard cell height (e.g., 10.0 units)
    double nextFreeX;   // The x-coordinate where the next cell can be placed
};

class Legalizer {
public:
    Legalizer(Design* design, double coreW = 400.0, double coreH = 400.0);
    void run();

private:
    Design* design;
    std::vector<Row> rows;
    
    // Configuration
    double coreWidth;
    double coreHeight;
    double rowHeight = 10.0;
    double siteWidth = 1.0;
};
