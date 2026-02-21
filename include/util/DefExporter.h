#pragma once
#include "db/Design.h"
#include <string>

class DefExporter {
public:
    void write(Design& design, std::string filename);

    // NEW: Industry-standard DEF export with proper DBU scaling and die area
    void writeDEF(const std::string& filename, Design* design, double coreWidth, double coreHeight);
};
