#pragma once
#include "db/Design.h"
#include "db/Library.h"
#include <vector>
#include <string>
#include <unordered_map>

// ============================================================
// PHASE 5: RC Parasitic Extraction Engine
// Sweeps routed 3D wire paths to compute per-segment R/C,
// calculates Elmore delay, and exports IEEE SPEF files.
// ============================================================

// A single wire segment between two route points
struct WireSegment {
    int x1, y1, x2, y2;
    int layer;
    double length;      // microns
    double resistance;  // ohms
    double capacitance; // fF (femtofarads)
};

// All parasitic data for one net
struct NetParasitics {
    std::string netName;
    double totalR = 0.0;      // Total resistance (ohms)
    double totalC = 0.0;      // Total capacitance (fF)
    double elmoreDelay = 0.0; // Elmore delay (ps)
    std::vector<WireSegment> segments;
};

class SpefEngine {
public:
    // Extract parasitics from all routed nets
    void extract(Design& design);

    // Export IEEE-standard SPEF file
    void writeSpef(const std::string& filename, Design& design);

    // Back-annotate parasitics from an existing SPEF file.
    // Populates parasiticsMap so getWireDelay / getNetCap work without extract().
    // Returns true if at least one net was parsed.
    bool readSpef(const std::string& filename);

    // Look up extracted parasitics for a specific net
    const NetParasitics* getParasitics(const std::string& netName) const;

    // Get the Elmore wire delay for a net (in ps)
    double getWireDelay(const std::string& netName) const;

    // Get total downstream capacitance for a net (in fF)
    double getNetCap(const std::string& netName) const;

    // Stats
    int getExtractedNetCount() const { return (int)parasiticsMap.size(); }

private:
    std::unordered_map<std::string, NetParasitics> parasiticsMap;

    // Per-layer technology constants (45nm generic process)
    // Layer 1 (M1): thin local interconnect — highest R, highest C
    // Layer 2 (M2): intermediate metal
    // Layer 3 (M3): semi-global routing
    // Layer 4 (M4): global routing — thickest, lowest R
    static constexpr double layerSheetR[4]   = {0.10, 0.08, 0.06, 0.05}; // ohms/square
    static constexpr double layerCapPerUm[4] = {0.20, 0.17, 0.14, 0.12}; // fF/um
    static constexpr double wireWidthUm      = 0.10;  // 100nm wire width
    static constexpr double gridPitchUm      = 0.14;  // Grid pitch in microns
    static constexpr double viaCap           = 0.05;  // fF per via
    static constexpr double viaRes           = 5.0;   // ohms per via
};
