#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct PinDef {
    std::string name;
    bool isOutput;
    double capacitance;
    // NEW: Relative Offset from center (in microns)
    double dx = 0.0;
    double dy = 0.0;
};

struct CellDef {
    std::string name;
    double area;
    double leakage;
    double width = 1.0;     // NEW: Physical Width (microns)
    double height = 1.0;    // NEW: Physical Height (microns)
    double delay;

    // NEW: Physics properties
    double leakagePower = 0.0; // in nW (nanoWatts)

    bool isSequential = false; // <--- NEW: True for Flip-Flops
    std::vector<PinDef> pins;

    // NEW: Physics Delay (Average of Rise/Fall)
    double intrinsicDelay = 10.0;
};

// ... (Keep Library class the same) ...

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
