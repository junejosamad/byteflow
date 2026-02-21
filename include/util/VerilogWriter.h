#pragma once
#include "db/Design.h"
#include <string>

class VerilogWriter {
public:
    void write(Design& design, std::string filename);
};
