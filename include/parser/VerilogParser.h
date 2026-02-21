#pragma once
#include <string>
#include <vector>
#include "db/Design.h"
#include "db/Library.h"

class VerilogParser {
public:
    // The main function: Reads a file and populates the 'design' object
    bool read(std::string filename, Design& design, Library& lib);

private:
    // Helper to clean up messy strings (remove spaces/commas)
    std::string clean(std::string s);
};
