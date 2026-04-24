#pragma once
#include "db/Design.h"
#include <vector>
#include <string>

// Coarse GCell: one tile in the global routing grid.
struct GCell {
    int   hUsage = 0, vUsage = 0;        // current routing usage
    int   hCapacity = 4, vCapacity = 4;  // track capacity per direction
    float hHistCost = 1.0f;              // history congestion cost (horizontal)
    float vHistCost = 1.0f;              // history congestion cost (vertical)
};

// Result returned by GlobalRouter::route().
struct GRouteResult {
    int    netsRouted    = 0;   // nets with at least one route guide assigned
    int    totalOverflow = 0;   // total edge overflow across all GCells
    int    overflowCells = 0;   // GCells with any overflow
    double routability   = 0.0; // fraction of edges that are not overflowed (0..1)
    std::vector<std::string> unroutedNets;  // nets that could not be routed
};

// Global router: builds a coarse GCell grid over a placed design,
// routes all signal nets using MST decomposition + Dijkstra,
// iteratively reduces congestion via history costs, and stores per-net
// RouteGuide bounding boxes on Net::routeGuides for the detailed router.
class GlobalRouter {
public:
    GRouteResult route(Design* chip,
                       int gcellsX  = 10,
                       int gcellsY  = 10,
                       int maxIter  = 3);

    // Accessors for inspection / testing
    int gcellsX() const { return gcX_; }
    int gcellsY() const { return gcY_; }
    const GCell& gcell(int row, int col) const { return grid_[row * gcX_ + col]; }

private:
    int    gcX_ = 0, gcY_ = 0;
    double coreW_ = 0, coreH_ = 0;
    double cellW_ = 0, cellH_ = 0;
    std::vector<GCell> grid_;

    // Grid helpers
    int    idx(int r, int c)  const { return r * gcX_ + c; }
    GCell& at(int r, int c)         { return grid_[idx(r, c)]; }
    const GCell& at(int r, int c) const { return grid_[idx(r, c)]; }

    std::pair<int,int> pinToGCell(double x, double y) const;

    void clearUsage();
    void updateHistoryCosts(float penalty = 2.0f);
    int  computeTotalOverflow() const;
    int  computeOverflowCells() const;

    // MST over GCell positions (Prim's, Manhattan distance) → list of index pairs
    std::vector<std::pair<int,int>> buildMST(
        const std::vector<std::pair<int,int>>& pts) const;

    // Congestion-aware Dijkstra between two GCells
    std::vector<std::pair<int,int>> routePair(int r1, int c1, int r2, int c2) const;

    // Accumulate path into GCell usage counters
    void applyPath(const std::vector<std::pair<int,int>>& path);

    // Convert a GCell path to a physical RouteGuide (bounding box, expanded by 1 GCell)
    RouteGuide pathToGuide(const std::vector<std::pair<int,int>>& path) const;
};
