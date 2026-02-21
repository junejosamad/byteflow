#pragma once
#include "db/Design.h"
#include <string>

class ScriptExporter {
public:
    void write(Design& design, std::string filename);
};
