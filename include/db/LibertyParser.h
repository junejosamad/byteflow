#pragma once
#include "db/Library.h"
#include <string>

class LibertyParser {
public:
    // Takes 'Library&' because that is where CellDefs live
    void parse(std::string filename, Library& lib);
};
