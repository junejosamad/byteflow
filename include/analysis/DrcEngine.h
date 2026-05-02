#pragma once
#include "db/Design.h"
#include <string>
#include <vector>

// ============================================================
// DRC Violation Types
// ============================================================
enum class DrcViolationType {
    SHORT,          // two nets overlap on the same layer
    MIN_SPACING,    // nets are too close (gap < rule) but not overlapping
    MIN_WIDTH,      // single wire narrower than the layer rule
    MIN_AREA,       // single rectangle area below minimum
    VIA_ENCLOSURE,  // insufficient metal enclosure around a via
};

struct DrcViolation {
    DrcViolationType type      = DrcViolationType::SHORT;
    int              layer     = 0;
    double           x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // violation bbox (design units)
    std::string      net1, net2;
    std::string      message;
};

// ============================================================
// Rule Deck
// ============================================================
struct LayerRule {
    int         layerIdx   = 0;
    std::string name;
    double      minWidth   = 0.0;   // design units
    double      minSpacing = 0.0;   // design units
    double      minArea    = 0.0;   // design units²
};

struct ViaRule {
    int         fromLayer = 0;
    int         toLayer   = 0;
    std::string name;
    double      enclosure = 0.0;   // design units (half)
    double      viaSize   = 0.0;   // design units (half)
};

struct DrcRuleDeck {
    std::vector<LayerRule> layerRules;
    std::vector<ViaRule>   viaRules;

    // Built-in sky130_hd defaults
    static DrcRuleDeck sky130();

    // Parse a .drc text file (values in nm; converted internally to design units)
    bool loadFromFile(const std::string& filename);

    const LayerRule* getLayerRule(int layerIdx) const;
    const ViaRule*   getViaRule(int from, int to) const;
};

// ============================================================
// DRC Report
// ============================================================
struct DrcReport {
    std::vector<DrcViolation> violations;

    int shortCount()   const;
    int spacingCount() const;
    int widthCount()   const;
    int areaCount()    const;
    int totalCount()   const { return (int)violations.size(); }

    void print(int maxPrint = 30) const;
};

// ============================================================
// DRC Engine
// ============================================================
class DrcEngine {
public:
    // Run DRC with sky130 built-in defaults
    DrcReport runDrc(Design* chip);

    // Run DRC with a custom rule deck
    DrcReport runDrc(Design* chip, const DrcRuleDeck& rules);

    // Legacy stub — kept for backward compatibility
    void runDRC(Design& design) { runDrc(&design); }

private:
    // 1 design unit = 100 nm (matches GdsExporter SCALE=100)
    // Wire geometry: HALF_WIDTH=7nm — keeps rects non-degenerate and ensures
    // adjacent-cell gap (100nm - 14nm = 86nm) exceeds minSpacing (80nm).
    static constexpr double SCALE      = 100.0;
    static constexpr double HALF_WIDTH =  0.07; // design units = 7 nm
    static constexpr double VIA_HALF   =  0.07; // design units = 7 nm

    struct DrcRect {
        double      x1, y1, x2, y2;
        int         layer;
        std::string netName;
        bool        isVia = false;
    };

    std::vector<DrcRect> extractRects(Design* chip) const;

    void checkMinWidth(const std::vector<DrcRect>&, const DrcRuleDeck&, DrcReport&) const;
    void checkMinArea (const std::vector<DrcRect>&, const DrcRuleDeck&, DrcReport&) const;
    void checkSpacing (const std::vector<DrcRect>&, const DrcRuleDeck&, DrcReport&) const;
};
