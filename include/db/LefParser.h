#pragma once
#include <string>
#include "db/Design.h"

class LefParser {
public:
    void parse(const std::string& filename, Design* design);
};
