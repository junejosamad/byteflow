#pragma once
#include "db/Design.h"
#include <string>
#include <vector>

// ============================================================
// LVS Mismatch Types
// ============================================================
enum class LvsMismatchType {
    UNPLACED_INSTANCE,  // cell exists in netlist but was not placed
    UNCONNECTED_PIN,    // pin has no net assignment (floating)
    UNROUTED_NET,       // net has ≥2 pins but no routing path
    OPEN_CIRCUIT,       // pin not physically reached by its net's routing
};

struct LvsMismatch {
    LvsMismatchType type;
    std::string     instName;
    std::string     netName;
    std::string     pinName;
    std::string     message;
};

// ============================================================
// LVS Report
// ============================================================
struct LvsReport {
    std::vector<LvsMismatch> mismatches;

    // Design-level statistics
    int instanceCount    = 0;
    int netCount         = 0;
    int routedNetCount   = 0;
    int totalPinCount    = 0;
    int connectedPinCount= 0;

    bool clean() const { return mismatches.empty(); }

    int unplacedCount()       const;
    int unconnectedPinCount() const;
    int unroutedCount()       const;
    int openCircuitCount()    const;
    int totalCount()          const { return (int)mismatches.size(); }

    void print(int maxPrint = 30) const;
};

// ============================================================
// LVS Engine
// ============================================================
class LvsEngine {
public:
    LvsReport runLvs(Design* chip);

private:
    // 1 design unit = 100 nm (matches GdsExporter/DrcEngine convention)
    static constexpr double HALF_WIDTH = 10.0;   // design units = 1000 nm

    // Check 1: every instance is placed
    void checkPlacement(Design* chip, LvsReport& report) const;

    // Check 2: every pin has a net assignment
    void checkPinConnections(Design* chip, LvsReport& report) const;

    // Check 3: every multi-pin net has been routed
    void checkNetRouting(Design* chip, LvsReport& report) const;

    // Check 4: each pin is physically reached by its net's routing
    //          Uses a bounding-box coverage check with 2× HALF_WIDTH margin.
    void checkPhysicalCoverage(Design* chip, LvsReport& report) const;
};
