#include "route/GlobalRouter.h"
#include <iostream>
#include <queue>
#include <limits>
#include <climits>
#include <algorithm>
#include <cmath>
#include <numeric>

// ─────────────────────────────────────────────────────────────────────────────
// Grid helpers
// ─────────────────────────────────────────────────────────────────────────────

std::pair<int,int> GlobalRouter::pinToGCell(double x, double y) const {
    int c = std::max(0, std::min(gcX_ - 1, (int)(x / cellW_)));
    int r = std::max(0, std::min(gcY_ - 1, (int)(y / cellH_)));
    return {r, c};
}

void GlobalRouter::clearUsage() {
    for (auto& gc : grid_) { gc.hUsage = 0; gc.vUsage = 0; }
}

void GlobalRouter::updateHistoryCosts(float penalty) {
    for (int r = 0; r < gcY_; ++r) {
        for (int c = 0; c < gcX_; ++c) {
            GCell& gc = at(r, c);
            if (gc.hUsage > gc.hCapacity)
                gc.hHistCost += penalty * (float)(gc.hUsage - gc.hCapacity);
            if (gc.vUsage > gc.vCapacity)
                gc.vHistCost += penalty * (float)(gc.vUsage - gc.vCapacity);
        }
    }
}

int GlobalRouter::computeTotalOverflow() const {
    int total = 0;
    for (const auto& gc : grid_) {
        total += std::max(0, gc.hUsage - gc.hCapacity);
        total += std::max(0, gc.vUsage - gc.vCapacity);
    }
    return total;
}

int GlobalRouter::computeOverflowCells() const {
    int count = 0;
    for (const auto& gc : grid_)
        if (gc.hUsage > gc.hCapacity || gc.vUsage > gc.vCapacity) ++count;
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// MST (Prim's algorithm, Manhattan distance between GCell centres)
// Returns: list of {i, j} index pairs into `pts` that form the spanning tree.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::pair<int,int>> GlobalRouter::buildMST(
    const std::vector<std::pair<int,int>>& pts) const
{
    int n = (int)pts.size();
    if (n <= 1) return {};

    std::vector<int> inTree(n, 0);
    std::vector<int> dist(n, INT_MAX);
    std::vector<int> parent(n, -1);
    dist[0] = 0;

    for (int iter = 0; iter < n; ++iter) {
        int u = -1;
        for (int i = 0; i < n; ++i)
            if (!inTree[i] && (u == -1 || dist[i] < dist[u])) u = i;
        inTree[u] = 1;

        for (int v = 0; v < n; ++v) {
            if (inTree[v]) continue;
            int d = std::abs(pts[u].first  - pts[v].first)
                  + std::abs(pts[u].second - pts[v].second);
            if (d < dist[v]) { dist[v] = d; parent[v] = u; }
        }
    }

    std::vector<std::pair<int,int>> edges;
    for (int i = 1; i < n; ++i)
        if (parent[i] >= 0) edges.push_back({parent[i], i});
    return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dijkstra on the 4-connected GCell graph with congestion-aware edge costs.
// Returns the path as a list of (row, col) pairs, start → goal.
// Returns empty if goal is unreachable.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::pair<int,int>> GlobalRouter::routePair(
    int r1, int c1, int r2, int c2) const
{
    if (r1 == r2 && c1 == c2) return {{r1, c1}};

    int N = gcY_ * gcX_;
    const float INF = std::numeric_limits<float>::infinity();
    std::vector<float> distArr(N, INF);
    std::vector<int>   prev(N, -1);

    // Priority queue: {cost, flat_index}
    using PQNode = std::pair<float, int>;
    std::priority_queue<PQNode, std::vector<PQNode>, std::greater<PQNode>> pq;

    int start = r1 * gcX_ + c1;
    int goal  = r2 * gcX_ + c2;
    distArr[start] = 0.0f;
    pq.push({0.0f, start});

    // 4-connected neighbours: {Δrow, Δcol}
    const int DR[] = {-1, 1,  0, 0};
    const int DC[] = { 0, 0, -1, 1};

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > distArr[u]) continue;
        if (u == goal) break;

        int r = u / gcX_, c = u % gcX_;
        for (int dir = 0; dir < 4; ++dir) {
            int nr = r + DR[dir], nc = c + DC[dir];
            if (nr < 0 || nr >= gcY_ || nc < 0 || nc >= gcX_) continue;

            // Edge cost from (r,c): use history cost of the FROM cell.
            const GCell& gc = at(r, c);
            float edgeCost;
            if (dir < 2) {   // vertical move (row changes)
                float overflow = (float)std::max(0, gc.vUsage - gc.vCapacity);
                edgeCost = gc.vHistCost + 5.0f * overflow;
            } else {          // horizontal move (col changes)
                float overflow = (float)std::max(0, gc.hUsage - gc.hCapacity);
                edgeCost = gc.hHistCost + 5.0f * overflow;
            }
            edgeCost = std::max(edgeCost, 0.01f);

            float nd = d + edgeCost;
            int v = nr * gcX_ + nc;
            if (nd < distArr[v]) {
                distArr[v] = nd;
                prev[v] = u;
                pq.push({nd, v});
            }
        }
    }

    if (distArr[goal] == INF) return {};  // unreachable

    // Reconstruct path
    std::vector<std::pair<int,int>> path;
    for (int u = goal; u != -1; u = prev[u])
        path.push_back({u / gcX_, u % gcX_});
    std::reverse(path.begin(), path.end());
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// Increment GCell edge usage counters along a path.
// Horizontal step → hUsage at the leftmost of the two GCells.
// Vertical   step → vUsage at the bottom of the two GCells.
// ─────────────────────────────────────────────────────────────────────────────

void GlobalRouter::applyPath(const std::vector<std::pair<int,int>>& path) {
    for (int i = 0; i + 1 < (int)path.size(); ++i) {
        int r1 = path[i].first,   c1 = path[i].second;
        int r2 = path[i+1].first, c2 = path[i+1].second;
        GCell& gc = at(std::min(r1, r2), std::min(c1, c2));
        if (r1 == r2) gc.hUsage++;   // horizontal step
        else           gc.vUsage++;   // vertical step
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Convert a list of GCell positions to a physical RouteGuide bounding box.
// Expands by 1 GCell in every direction for routing slack.
// ─────────────────────────────────────────────────────────────────────────────

RouteGuide GlobalRouter::pathToGuide(
    const std::vector<std::pair<int,int>>& path) const
{
    if (path.empty()) return {};
    int rMin = path[0].first,  rMax = path[0].first;
    int cMin = path[0].second, cMax = path[0].second;
    for (auto& [r, c] : path) {
        rMin = std::min(rMin, r); rMax = std::max(rMax, r);
        cMin = std::min(cMin, c); cMax = std::max(cMax, c);
    }
    // Expand by 1 GCell
    rMin = std::max(0,      rMin - 1); cMin = std::max(0,      cMin - 1);
    rMax = std::min(gcY_-1, rMax + 1); cMax = std::min(gcX_-1, cMax + 1);

    RouteGuide g;
    g.xMin = cMin * cellW_;
    g.yMin = rMin * cellH_;
    g.xMax = (cMax + 1) * cellW_;
    g.yMax = (rMax + 1) * cellH_;
    // Prefer horizontal layer for wide nets, vertical for tall nets
    g.preferredLayer = ((cMax - cMin) >= (rMax - rMin)) ? 1 : 2;
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main entry point
// ─────────────────────────────────────────────────────────────────────────────

GRouteResult GlobalRouter::route(Design* chip, int gcellsX, int gcellsY, int maxIter) {
    GRouteResult res;
    if (!chip || chip->instances.empty()) return res;

    // Initialise grid
    gcX_  = std::max(1, gcellsX);
    gcY_  = std::max(1, gcellsY);
    coreW_ = chip->coreWidth  > 0 ? chip->coreWidth  : 1000.0;
    coreH_ = chip->coreHeight > 0 ? chip->coreHeight : 1000.0;
    cellW_ = coreW_ / gcX_;
    cellH_ = coreH_ / gcY_;
    grid_.assign(gcY_ * gcX_, GCell{});

    // Capacity heuristic: at least 2; scale with net density
    int nSignalNets = 0;
    for (Net* n : chip->nets)
        if (n->name != "VDD" && n->name != "VSS" && n->connectedPins.size() >= 2)
            ++nSignalNets;

    int baseCap = std::max(2, nSignalNets / (gcX_ * gcY_) + 2);
    for (auto& gc : grid_) { gc.hCapacity = baseCap; gc.vCapacity = baseCap; }

    std::cout << "  [GlobalRoute] Grid: " << gcX_ << "x" << gcY_
              << "  capacity=" << baseCap
              << "  signal_nets=" << nSignalNets << "\n";

    // Build list of routable nets
    std::vector<Net*> signalNets;
    signalNets.reserve(nSignalNets);
    for (Net* n : chip->nets) {
        if (n->name == "VDD" || n->name == "VSS") continue;
        if (n->connectedPins.size() < 2) continue;
        signalNets.push_back(n);
    }

    // Per-net per-MST-edge paths (rewritten each iteration)
    std::vector<std::vector<std::vector<std::pair<int,int>>>> netPaths(signalNets.size());

    int prevOverflow = INT_MAX;
    for (int iter = 0; iter < maxIter; ++iter) {
        clearUsage();

        for (int ni = 0; ni < (int)signalNets.size(); ++ni) {
            Net* net = signalNets[ni];
            netPaths[ni].clear();

            // Collect unique GCell positions for this net's pins
            std::vector<std::pair<int,int>> gcPins;
            for (Pin* p : net->connectedPins) {
                if (!p || !p->inst) continue;
                auto gc = pinToGCell(p->getAbsX(), p->getAbsY());
                bool dup = false;
                for (auto& ex : gcPins) if (ex == gc) { dup = true; break; }
                if (!dup) gcPins.push_back(gc);
            }
            if (gcPins.size() < 2) continue;

            // MST decomposition: route each MST edge as a 2-pin problem
            auto mstEdges = buildMST(gcPins);
            for (auto& [i, j] : mstEdges) {
                auto path = routePair(gcPins[i].first, gcPins[i].second,
                                      gcPins[j].first, gcPins[j].second);
                if (!path.empty()) {
                    applyPath(path);
                    netPaths[ni].push_back(std::move(path));
                }
            }
        }

        int overflow  = computeTotalOverflow();
        int overCells = computeOverflowCells();
        std::cout << "  [GlobalRoute] Iter " << (iter + 1)
                  << ": overflow=" << overflow << "  cells=" << overCells << "\n";

        if (overflow == 0) break;
        if (overflow >= prevOverflow && iter > 0) break;  // no improvement
        prevOverflow = overflow;
        if (iter < maxIter - 1) updateHistoryCosts(2.0f);
    }

    // Assign route guides to nets
    for (int ni = 0; ni < (int)signalNets.size(); ++ni) {
        Net* net = signalNets[ni];
        net->routeGuides.clear();

        // Merge all MST-edge paths into a single bounding-box guide per net
        std::vector<std::pair<int,int>> allCells;
        for (auto& path : netPaths[ni])
            for (auto& rc : path) allCells.push_back(rc);

        if (!allCells.empty()) {
            net->routeGuides.push_back(pathToGuide(allCells));
            ++res.netsRouted;
        } else {
            res.unroutedNets.push_back(net->name);
        }
    }

    res.totalOverflow = computeTotalOverflow();
    res.overflowCells = computeOverflowCells();

    // routability = fraction of GCell edges without overflow
    int totalEdges    = 2 * gcX_ * gcY_;
    int overflowEdges = 0;
    for (const auto& gc : grid_) {
        if (gc.hUsage > gc.hCapacity) ++overflowEdges;
        if (gc.vUsage > gc.vCapacity) ++overflowEdges;
    }
    res.routability = totalEdges > 0 ? 1.0 - (double)overflowEdges / totalEdges : 1.0;

    std::cout << "  [GlobalRoute] Done: " << res.netsRouted << "/" << nSignalNets
              << " nets routed"
              << "  overflow=" << res.totalOverflow
              << "  routability=" << (int)(res.routability * 100) << "%\n";

    return res;
}
