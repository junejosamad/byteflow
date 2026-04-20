#pragma once
#include "db/Design.h"
#include <vector>

// Compact A* search node — 12 bytes
struct AStarNode {
    int   idx;
    float gCost;
    float fCost;
    bool operator>(const AStarNode& o) const { return fCost > o.fCost; }
};

class RouteEngine {
public:
    void runRouting(Design& design, int gridW, int gridH);

private:
    // Pre-allocated arrays reused across calls — prevents heap fragmentation
    std::vector<double>    astar_minCost;
    std::vector<int>       astar_parentIdx;
    std::vector<int>       astar_searchId;
    std::vector<int>       astar_netUsedId; // generational net-ownership tracker (replaces std::set)
    std::vector<double>    grid_history;
    std::vector<int>       grid_obstacles;
    std::vector<int>       grid_usage;      // per-iteration usage map
    std::vector<AStarNode> astar_pq_buf;   // backing store for priority queue heap
    std::vector<Point>     astar_segment;  // backing store for backtrack path (replaces local segment)
    int                    astar_capacity = 0;

    void ensureAstarCapacity(int totalNodes);
};
