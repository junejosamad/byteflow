#pragma once
#include "db/Design.h"
#include <iostream>

class PowerEngine {
public:
    // Calculates and prints the total power consumption
    void reportPower(Design& design, double supplyVoltage, double frequencyMHz);
};
