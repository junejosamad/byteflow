#pragma once
#include "db/Design.h"
#include <string>
#include <fstream>

class SvgExporter {
public:
    // Exports the current design state to an .svg file
    void exportLayout(Design& design, std::string filename, double coreW, double coreH);
};
