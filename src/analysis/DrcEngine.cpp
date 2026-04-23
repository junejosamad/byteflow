#include "analysis/DrcEngine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <map>

// ============================================================
// DrcRuleDeck — sky130 built-in defaults
// ============================================================

static LayerRule makeLayerRule(int idx, const std::string& name,
                                double minWidthNm, double minSpacingNm,
                                double minAreaNm2) {
    constexpr double SCALE = 100.0;
    LayerRule r;
    r.layerIdx   = idx;
    r.name       = name;
    r.minWidth   = minWidthNm   / SCALE;
    r.minSpacing = minSpacingNm / SCALE;
    r.minArea    = minAreaNm2   / (SCALE * SCALE);
    return r;
}

static ViaRule makeViaRule(int from, int to, const std::string& name,
                            double enclosureNm, double viaSizeNm) {
    constexpr double SCALE = 100.0;
    ViaRule r;
    r.fromLayer  = from;
    r.toLayer    = to;
    r.name       = name;
    r.enclosure  = enclosureNm  / SCALE;
    r.viaSize    = viaSizeNm    / SCALE;
    return r;
}

DrcRuleDeck DrcRuleDeck::sky130() {
    DrcRuleDeck deck;
    deck.layerRules = {
        makeLayerRule(1, "li1",  170,  170,  56100),
        makeLayerRule(2, "met1", 140,  140,  83000),
        makeLayerRule(3, "met2", 140,  140,  67600),
        makeLayerRule(4, "met3", 300,  300,  90000),
        makeLayerRule(5, "met4", 300,  300,  90000),
        makeLayerRule(6, "met5", 1600, 1600, 900000),
    };
    deck.viaRules = {
        makeViaRule(1, 2, "mcon",  60,  170),
        makeViaRule(2, 3, "via1",  55,  150),
        makeViaRule(3, 4, "via2",  65,  200),
        makeViaRule(4, 5, "via3",  65,  200),
        makeViaRule(5, 6, "via4", 310,  800),
    };
    return deck;
}

const LayerRule* DrcRuleDeck::getLayerRule(int layerIdx) const {
    for (const LayerRule& r : layerRules)
        if (r.layerIdx == layerIdx) return &r;
    return nullptr;
}

const ViaRule* DrcRuleDeck::getViaRule(int from, int to) const {
    for (const ViaRule& r : viaRules)
        if ((r.fromLayer == from && r.toLayer == to) ||
            (r.fromLayer == to   && r.toLayer == from)) return &r;
    return nullptr;
}

bool DrcRuleDeck::loadFromFile(const std::string& filename) {
    std::ifstream f(filename);
    if (!f) return false;

    constexpr double SCALE = 100.0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "layer") {
            LayerRule r;
            std::string kv;
            ss >> r.layerIdx >> r.name;
            while (ss >> kv) {
                auto eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string key = kv.substr(0, eq);
                double val = std::stod(kv.substr(eq + 1));
                if (key == "min_width")   r.minWidth   = val / SCALE;
                if (key == "min_spacing") r.minSpacing = val / SCALE;
                if (key == "min_area")    r.minArea    = val / (SCALE * SCALE);
            }
            layerRules.push_back(r);
        } else if (token == "via") {
            ViaRule r;
            std::string kv;
            ss >> r.fromLayer >> r.toLayer >> r.name;
            while (ss >> kv) {
                auto eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string key = kv.substr(0, eq);
                double val = std::stod(kv.substr(eq + 1));
                if (key == "enclosure")  r.enclosure = val / SCALE;
                if (key == "via_size")   r.viaSize   = val / SCALE;
            }
            viaRules.push_back(r);
        }
    }
    return !layerRules.empty();
}

// ============================================================
// DrcReport
// ============================================================

int DrcReport::shortCount()   const {
    int n = 0;
    for (const auto& v : violations) if (v.type == DrcViolationType::SHORT)       n++;
    return n;
}
int DrcReport::spacingCount() const {
    int n = 0;
    for (const auto& v : violations) if (v.type == DrcViolationType::MIN_SPACING) n++;
    return n;
}
int DrcReport::widthCount()   const {
    int n = 0;
    for (const auto& v : violations) if (v.type == DrcViolationType::MIN_WIDTH)   n++;
    return n;
}
int DrcReport::areaCount()    const {
    int n = 0;
    for (const auto& v : violations) if (v.type == DrcViolationType::MIN_AREA)    n++;
    return n;
}

void DrcReport::print(int maxPrint) const {
    std::cout << "\n=== DRC REPORT ===\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Total violations : " << totalCount()   << "\n";
    std::cout << "  Shorts           : " << shortCount()   << "\n";
    std::cout << "  Min Spacing      : " << spacingCount() << "\n";
    std::cout << "  Min Width        : " << widthCount()   << "\n";
    std::cout << "  Min Area         : " << areaCount()    << "\n";
    if (violations.empty()) {
        std::cout << "  DRC PASSED — no violations.\n";
    } else {
        std::cout << "\n  Top violations (up to " << maxPrint << "):\n";
        int shown = 0;
        for (const auto& v : violations) {
            if (shown++ >= maxPrint) break;
            const char* typeName = "UNKNOWN";
            switch (v.type) {
                case DrcViolationType::SHORT:       typeName = "SHORT";       break;
                case DrcViolationType::MIN_SPACING: typeName = "MIN_SPACING"; break;
                case DrcViolationType::MIN_WIDTH:   typeName = "MIN_WIDTH";   break;
                case DrcViolationType::MIN_AREA:    typeName = "MIN_AREA";    break;
                case DrcViolationType::VIA_ENCLOSURE: typeName = "VIA_ENC";  break;
            }
            std::cout << "  [" << typeName << "] L" << v.layer
                      << " (" << v.x1 << "," << v.y1 << ")-(" << v.x2 << "," << v.y2 << ")"
                      << "  " << v.net1;
            if (!v.net2.empty()) std::cout << " vs " << v.net2;
            std::cout << "  " << v.message << "\n";
        }
        if ((int)violations.size() > maxPrint)
            std::cout << "  ... " << (violations.size() - maxPrint) << " more\n";
    }
    std::cout << "==================\n";
}

// ============================================================
// Geometry extraction
// ============================================================

std::vector<DrcEngine::DrcRect> DrcEngine::extractRects(Design* chip) const {
    std::vector<DrcRect> rects;

    for (Net* net : chip->nets) {
        if (net->routePath.size() < 2) continue;

        for (size_t i = 0; i + 1 < net->routePath.size(); i++) {
            const Point& p1 = net->routePath[i];
            const Point& p2 = net->routePath[i + 1];

            if (p1.layer == p2.layer) {
                // Wire on one layer — expand centerline by HALF_WIDTH
                DrcRect r;
                r.layer   = p1.layer;
                r.netName = net->name;
                r.isVia   = false;
                r.x1 = std::min(p1.x, p2.x) - HALF_WIDTH;
                r.y1 = std::min(p1.y, p2.y) - HALF_WIDTH;
                r.x2 = std::max(p1.x, p2.x) + HALF_WIDTH;
                r.y2 = std::max(p1.y, p2.y) + HALF_WIDTH;
                // Ensure non-degenerate
                if (r.x2 > r.x1 && r.y2 > r.y1)
                    rects.push_back(r);
            } else {
                // Via — square at p2 on the higher layer
                int hiLayer = std::max(p1.layer, p2.layer);
                DrcRect r;
                r.layer   = hiLayer;
                r.netName = net->name;
                r.isVia   = true;
                r.x1 = p2.x - VIA_HALF;
                r.y1 = p2.y - VIA_HALF;
                r.x2 = p2.x + VIA_HALF;
                r.y2 = p2.y + VIA_HALF;
                if (r.x2 > r.x1 && r.y2 > r.y1)
                    rects.push_back(r);
            }
        }
    }
    return rects;
}

// ============================================================
// Check: Min Width
// ============================================================

void DrcEngine::checkMinWidth(const std::vector<DrcRect>& rects,
                               const DrcRuleDeck& rules, DrcReport& report) const {
    constexpr int CAP = 200;
    int found = 0;
    for (const DrcRect& r : rects) {
        if (r.isVia) continue;
        const LayerRule* lr = rules.getLayerRule(r.layer);
        if (!lr || lr->minWidth <= 0) continue;
        double w = r.x2 - r.x1;
        double h = r.y2 - r.y1;
        double narrow = std::min(w, h);
        if (narrow < lr->minWidth && found < CAP) {
            DrcViolation v;
            v.type    = DrcViolationType::MIN_WIDTH;
            v.layer   = r.layer;
            v.x1 = r.x1; v.y1 = r.y1; v.x2 = r.x2; v.y2 = r.y2;
            v.net1    = r.netName;
            v.message = "width=" + std::to_string((int)(narrow * SCALE))
                      + "nm  rule=" + std::to_string((int)(lr->minWidth * SCALE)) + "nm";
            report.violations.push_back(v);
            found++;
        }
    }
}

// ============================================================
// Check: Min Area
// ============================================================

void DrcEngine::checkMinArea(const std::vector<DrcRect>& rects,
                              const DrcRuleDeck& rules, DrcReport& report) const {
    constexpr int CAP = 200;
    int found = 0;
    for (const DrcRect& r : rects) {
        if (r.isVia) continue;
        const LayerRule* lr = rules.getLayerRule(r.layer);
        if (!lr || lr->minArea <= 0) continue;
        double area = (r.x2 - r.x1) * (r.y2 - r.y1);
        if (area < lr->minArea && found < CAP) {
            DrcViolation v;
            v.type    = DrcViolationType::MIN_AREA;
            v.layer   = r.layer;
            v.x1 = r.x1; v.y1 = r.y1; v.x2 = r.x2; v.y2 = r.y2;
            v.net1    = r.netName;
            v.message = "area=" + std::to_string((int)(area * SCALE * SCALE))
                      + "nm2  rule=" + std::to_string((int)(lr->minArea * SCALE * SCALE)) + "nm2";
            report.violations.push_back(v);
            found++;
        }
    }
}

// ============================================================
// Check: Spacing + Short
//
// For each layer, sort rects by x1, then sweep for pairs where
// overlap in x is possible. Compute exact gap; classify SHORT
// (overlap) or MIN_SPACING (gap < rule) for different nets.
// ============================================================

void DrcEngine::checkSpacing(const std::vector<DrcRect>& rects,
                              const DrcRuleDeck& rules, DrcReport& report) const {
    // Group rects by layer
    std::map<int, std::vector<const DrcRect*>> byLayer;
    for (const DrcRect& r : rects)
        byLayer[r.layer].push_back(&r);

    constexpr int CAP_PER_TYPE = 300;
    int shortFound   = 0;
    int spacingFound = 0;

    for (auto& [layer, layerRects] : byLayer) {
        const LayerRule* lr = rules.getLayerRule(layer);
        double minSpacing = lr ? lr->minSpacing : 0.0;

        // Sort by x1 for sweep
        std::vector<const DrcRect*> sorted = layerRects;
        std::sort(sorted.begin(), sorted.end(),
                  [](const DrcRect* a, const DrcRect* b) { return a->x1 < b->x1; });

        int n = (int)sorted.size();
        for (int i = 0; i < n; i++) {
            const DrcRect* a = sorted[i];
            for (int j = i + 1; j < n; j++) {
                const DrcRect* b = sorted[j];

                // Early exit: b is too far right to interact with a
                double needed = a->x2 + std::max(minSpacing, 1.0);
                if (b->x1 > needed) break;

                // Skip same-net pairs
                if (a->netName == b->netName) continue;

                // Compute axis-aligned gaps
                double xGap = std::max(0.0, std::max(a->x1 - b->x2, b->x1 - a->x2));
                double yGap = std::max(0.0, std::max(a->y1 - b->y2, b->y1 - a->y2));

                // Bounding box of the pair
                double bx1 = std::min(a->x1, b->x1);
                double by1 = std::min(a->y1, b->y1);
                double bx2 = std::max(a->x2, b->x2);
                double by2 = std::max(a->y2, b->y2);

                if (xGap < 0.01 && yGap < 0.01) {
                    // Rectangles overlap — SHORT
                    if (shortFound < CAP_PER_TYPE) {
                        DrcViolation v;
                        v.type  = DrcViolationType::SHORT;
                        v.layer = layer;
                        v.x1 = bx1; v.y1 = by1; v.x2 = bx2; v.y2 = by2;
                        v.net1 = a->netName;
                        v.net2 = b->netName;
                        v.message = "nets overlap on layer " + std::to_string(layer);
                        report.violations.push_back(v);
                        shortFound++;
                    }
                } else if (minSpacing > 0) {
                    // Check spacing: violation if rects are adjacent in exactly one axis
                    // Standard DRC: gap must be >= minSpacing when projections overlap
                    bool xOverlap = (xGap < 0.01);
                    bool yOverlap = (yGap < 0.01);
                    bool violating = (xOverlap && yGap > 0 && yGap < minSpacing)
                                  || (yOverlap && xGap > 0 && xGap < minSpacing);
                    if (violating && spacingFound < CAP_PER_TYPE) {
                        DrcViolation v;
                        v.type  = DrcViolationType::MIN_SPACING;
                        v.layer = layer;
                        v.x1 = bx1; v.y1 = by1; v.x2 = bx2; v.y2 = by2;
                        v.net1 = a->netName;
                        v.net2 = b->netName;
                        double gap = xOverlap ? yGap : xGap;
                        v.message = "gap=" + std::to_string((int)(gap * SCALE))
                                  + "nm  rule=" + std::to_string((int)(minSpacing * SCALE)) + "nm";
                        report.violations.push_back(v);
                        spacingFound++;
                    }
                }
            }
        }
    }
}

// ============================================================
// Public API
// ============================================================

DrcReport DrcEngine::runDrc(Design* chip) {
    return runDrc(chip, DrcRuleDeck::sky130());
}

DrcReport DrcEngine::runDrc(Design* chip, const DrcRuleDeck& rules) {
    DrcReport report;

    std::vector<DrcRect> rects = extractRects(chip);
    if (rects.empty()) {
        std::cout << "  [DRC] No routing geometry found — skipping checks.\n";
        return report;
    }

    std::cout << "  [DRC] Checking " << rects.size() << " rectangles...\n";
    checkMinWidth(rects, rules, report);
    checkMinArea (rects, rules, report);
    checkSpacing (rects, rules, report);

    std::cout << "  [DRC] Done — "
              << report.totalCount()   << " violations ("
              << report.shortCount()   << " shorts, "
              << report.spacingCount() << " spacing, "
              << report.widthCount()   << " width, "
              << report.areaCount()    << " area)\n";
    return report;
}
