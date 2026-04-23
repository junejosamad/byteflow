#pragma once
#include "db/Design.h"
#include <string>
#include <vector>

// ============================================================
// ERC Violation Types
// ============================================================
enum class ErcViolationType {
    FLOATING_INPUT,   // input pin's net has no output driver
    MULTIPLE_DRIVER,  // >= 2 output pins drive the same net (bus conflict)
    NO_POWER_PIN,     // VDD/VSS/VPWR/VGND pin has no net connection
};

struct ErcViolation {
    ErcViolationType type;
    std::string      instName;
    std::string      netName;
    std::string      pinName;
    std::string      message;
};

// ============================================================
// ERC Report
// ============================================================
struct ErcReport {
    std::vector<ErcViolation> violations;

    // Design-level statistics
    int instanceCount = 0;
    int netCount      = 0;
    int pinCount      = 0;

    bool clean() const { return violations.empty(); }

    int floatingInputCount()  const;
    int multipleDriverCount() const;
    int noPowerPinCount()     const;
    int totalCount()          const { return (int)violations.size(); }

    void print(int maxPrint = 30) const;
};

// ============================================================
// ERC Engine
// ============================================================
class ErcEngine {
public:
    ErcReport runErc(Design* chip);

private:
    static constexpr int CAP = 300;  // max violations per category

    // Check 1: every input pin's net has at least one output driver
    void checkFloatingInputs(Design* chip, ErcReport& report) const;

    // Check 2: no net has more than one output driver
    void checkMultipleDrivers(Design* chip, ErcReport& report) const;

    // Check 3: power/ground pins (VDD/VSS/VPWR/VGND/VCC/GND) are connected
    void checkPowerPins(Design* chip, ErcReport& report) const;
};
