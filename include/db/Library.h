#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

// ============================================================
// PHASE 5: Non-Linear Delay Model (NLDM) Lookup Table
// A 2D table indexed by input slew and output capacitance.
// Used by Liberty (.lib) timing arcs: cell_rise, cell_fall, etc.
// ============================================================
struct NldmTable {
    std::vector<double> index1; // Input transition times (slew, in ps)
    std::vector<double> index2; // Output capacitance values (in fF)
    std::vector<std::vector<double>> values; // Delay values [slew_idx][cap_idx]

    // Bilinear interpolation: given a slew and cap, return the delay.
    double lookup(double slew, double cap) const {
        if (index1.empty() || index2.empty() || values.empty()) return -1.0;

        // Clamp and find interpolation indices for slew (index1)
        auto clampAndFind = [](const std::vector<double>& idx, double val,
                               int& i0, int& i1, double& frac) {
            if (val <= idx.front()) { i0 = 0; i1 = 0; frac = 0.0; return; }
            if (val >= idx.back())  { i0 = (int)idx.size()-1; i1 = i0; frac = 0.0; return; }
            for (int i = 0; i < (int)idx.size()-1; ++i) {
                if (val >= idx[i] && val <= idx[i+1]) {
                    i0 = i; i1 = i+1;
                    frac = (val - idx[i]) / (idx[i+1] - idx[i]);
                    return;
                }
            }
            i0 = 0; i1 = 0; frac = 0.0;
        };

        int s0, s1, c0, c1;
        double sf, cf;
        clampAndFind(index1, slew, s0, s1, sf);
        clampAndFind(index2, cap,  c0, c1, cf);

        // Bilinear interpolation
        double v00 = values[s0][c0];
        double v01 = values[s0][c1];
        double v10 = values[s1][c0];
        double v11 = values[s1][c1];

        double top    = v00 + (v01 - v00) * cf;
        double bottom = v10 + (v11 - v10) * cf;
        return top + (bottom - top) * sf;
    }

    bool isValid() const { return !index1.empty() && !index2.empty() && !values.empty(); }
};

// ============================================================
// A Timing Arc: characterizes delay from one pin to another
// within a single cell (e.g., A -> Y in an AND2 gate).
// ============================================================
struct TimingArc {
    std::string fromPin;    // e.g. "A"
    std::string toPin;      // e.g. "Y"

    NldmTable cellRise;         // Delay for rising output
    NldmTable cellFall;         // Delay for falling output
    NldmTable riseTransition;   // Output slew for rising output
    NldmTable fallTransition;   // Output slew for falling output

    double setupTime = 0.0;    // For sequential cells (D -> CLK constraint)
    double holdTime  = 0.0;

    // Quick scalar lookup (worst-case of rise/fall)
    double getDelay(double inputSlew, double outputCap) const {
        double rise = cellRise.isValid()  ? cellRise.lookup(inputSlew, outputCap)  : -1.0;
        double fall = cellFall.isValid()  ? cellFall.lookup(inputSlew, outputCap)  : -1.0;
        if (rise < 0 && fall < 0) return -1.0;
        if (rise < 0) return fall;
        if (fall < 0) return rise;
        return std::max(rise, fall); // Worst-case
    }

    // Get output slew (worst-case of rise/fall)
    double getOutputSlew(double inputSlew, double outputCap) const {
        double rise = riseTransition.isValid() ? riseTransition.lookup(inputSlew, outputCap) : -1.0;
        double fall = fallTransition.isValid() ? fallTransition.lookup(inputSlew, outputCap) : -1.0;
        if (rise < 0 && fall < 0) return inputSlew; // Pass-through
        if (rise < 0) return fall;
        if (fall < 0) return rise;
        return std::max(rise, fall);
    }
};

// ============================================================
// Pin Definition (Physical + Electrical)
// ============================================================
struct PinDef {
    std::string name;
    bool isOutput;
    double capacitance;
    // Relative Offset from center (in microns)
    double dx = 0.0;
    double dy = 0.0;
};

// ============================================================
// Cell Definition (Logic + Timing + Physical)
// ============================================================
struct CellDef {
    std::string name;
    double area;
    double leakage;
    double width = 1.0;     // Physical Width (microns)
    double height = 1.0;    // Physical Height (microns)
    double delay;

    // Physics properties
    double leakagePower = 0.0; // in nW (nanoWatts)

    bool isSequential = false; // True for Flip-Flops
    std::vector<PinDef> pins;

    // Scalar Delay Fallback (Average of Rise/Fall)
    double intrinsicDelay = 10.0;

    // PHASE 5: NLDM Timing Arcs (one per input->output pin pair)
    std::vector<TimingArc> timingArcs;

    // Look up the timing arc for a given input->output pair
    const TimingArc* getTimingArc(const std::string& fromPin, const std::string& toPin) const {
        for (const auto& arc : timingArcs) {
            if (arc.fromPin == fromPin && arc.toPin == toPin) return &arc;
        }
        return nullptr;
    }

    // Get the best delay estimate: NLDM if available, else scalar fallback
    double getDelay(double inputSlew = 50.0, double outputCap = 5.0) const {
        double worstDelay = -1.0;
        for (const auto& arc : timingArcs) {
            double d = arc.getDelay(inputSlew, outputCap);
            if (d > worstDelay) worstDelay = d;
        }
        return (worstDelay > 0) ? worstDelay : intrinsicDelay;
    }
};

// ============================================================
// Library: Container for all Cell definitions
// ============================================================
class Library {
public:
    std::unordered_map<std::string, CellDef*> cells;

    void addCell(CellDef* cell) {
        cells[cell->name] = cell;
    }

    CellDef* getCell(std::string name) {
        if (cells.find(name) != cells.end()) return cells[name];
        return nullptr;
    }
};
