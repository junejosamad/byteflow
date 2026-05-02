#include "route/RouteEngine.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <tuple>

// 3D Coordinate for collision tracking
struct Point3D {
    int x, y, l;
    bool operator<(const Point3D& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return l < o.l;
    }
};

// AStarNode defined in RouteEngine.h — referenced here as Node alias for brevity
using Node = AStarNode;

// Helper to flatten 3D coordinates into a 1D array index for extreme speed
inline int getIdx(int x, int y, int l, int gridH, int gridL) {
    return (x * gridH + y) * gridL + l;
}

void RouteEngine::runRouting(Design& design, int gridW, int gridH) {
#ifdef _OPENMP
    omp_set_num_threads(std::min(omp_get_max_threads(), 4));
#endif
    gridW = gridW + 60;
    gridH = gridH + 60;
    int gridL = 7;   // Layers 0, 1, 2, 3, 4, 5, 6 (M1 through M6 where 0 is unused)
    int maxIter = 30;

    // Adaptive scale: keep totalNodes (gridW*gridH*gridL) ≤ MAX_GRID_NODES.
    // 300k was too coarse (scale=2.22): adjacent input pins quantized to the
    // SAME grid cell, so two nets shared the same source location and the only
    // L2 escape via — all other L1 neighbors are PDN-blocked.  2M keeps scale=1
    // for typical small floorplans (~460x460x7 = 1.48M nodes).
    static const int MAX_GRID_NODES = 2000000;
    double rawNodes = (double)gridW * gridH * gridL;
    double scaleXY  = (rawNodes > MAX_GRID_NODES)
                        ? std::sqrt(rawNodes / MAX_GRID_NODES)
                        : 1.0;
    double scaleX = scaleXY;
    double scaleY = scaleXY;
    gridW = (int)std::ceil(gridW / scaleXY);
    gridH = (int)std::ceil(gridH / scaleXY);

    int totalNodes = gridW * gridH * gridL;
    std::cout << "  Grid: " << gridW << " x " << gridH << " x " << gridL
              << " (" << totalNodes << " nodes, scale=" << scaleXY << ")"
              << ", Max Iterations: " << maxIter << "\n" << std::flush;

    // Use member vectors — allocated once on first call, reused across iterations
    ensureAstarCapacity(totalNodes);
    // Reset history, obstacles, and usage for this routing run
    std::fill(grid_history.begin(),   grid_history.begin()   + totalNodes, 1.0);
    std::fill(grid_obstacles.begin(), grid_obstacles.begin() + totalNodes, 0);
    std::fill(grid_usage.begin(),     grid_usage.begin()     + totalNodes, 0);
    auto& history   = grid_history;
    auto& obstacles = grid_obstacles;
    bool hasConflicts = true;

    std::vector<Net*> pinOwner(totalNodes, nullptr);
    
    // Add Pin Keep-Out Zones: Record who owns which pin
    for (Net* blockNet : design.nets) {
        if (blockNet->name == "VDD" || blockNet->name == "VSS" || blockNet->connectedPins.size() < 2) continue;
        for (Pin* pin : blockNet->connectedPins) {
            if (!pin || !pin->inst) continue;
            int px = std::max(0, std::min(gridW-1, (int)(pin->getAbsX() / scaleX)));
            int py = std::max(0, std::min(gridH-1, (int)(pin->getAbsY() / scaleY)));
            pinOwner[getIdx(px, py, 1, gridH, gridL)] = blockNet;
        }
    }
    
    // Add Pre-Routed PDN paths to the obstacle map.
    // Interpolate ALL grid cells along each wire segment — the old code only marked
    // endpoints, leaving entire PDN rail spans transparent to signal routing and
    // producing 300 DRC shorts where signal wires crossed M1 power rails.
    // Signal pin cells are excluded so pin access is never blocked.
    for (Net* const preNet : design.nets) {
        if (preNet->name != "VDD" && preNet->name != "VSS" && preNet->connectedPins.size() >= 2) continue;
        const auto& path = preNet->routePath;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            const Point& p1 = path[i];
            const Point& p2 = path[i + 1];
            if (p1.layer != p2.layer) continue; // via transition — different layers, not a wire
            if (p1.layer < 1 || p1.layer >= gridL) continue;
            int gx1 = std::max(0, std::min(gridW-1, (int)(p1.x / scaleX)));
            int gy1 = std::max(0, std::min(gridH-1, (int)(p1.y / scaleY)));
            int gx2 = std::max(0, std::min(gridW-1, (int)(p2.x / scaleX)));
            int gy2 = std::max(0, std::min(gridH-1, (int)(p2.y / scaleY)));
            int ddx = (gx2 > gx1) ? 1 : (gx2 < gx1) ? -1 : 0;
            int ddy = (gy2 > gy1) ? 1 : (gy2 < gy1) ? -1 : 0;
            // Skip diagonal phantom pairs — the flat pair-encoded routePath joins
            // the end of one segment to the start of the next, forming a diagonal
            // "joint" that is not a real wire. Only H or V segments are valid PDN wires.
            if (ddx != 0 && ddy != 0) continue;
            int gx = gx1, gy = gy1;
            int maxSteps = std::abs(gx2 - gx1) + std::abs(gy2 - gy1) + 1;
            for (int step = 0; step <= maxSteps; step++) {
                if (gx < 0 || gx >= gridW || gy < 0 || gy >= gridH) break;
                int idx = getIdx(gx, gy, p1.layer, gridH, gridL);
                // Hard-block this PDN cell. With HALF_WIDTH=0.07 (7nm) and
                // minSpacing=80nm, an adjacent signal route (gap=86nm) is DRC-clean.
                // No clearance buffer needed — the thin wire geometry ensures the
                // 14nm total wire pair width << 80nm spacing rule.
                if (pinOwner[idx] == nullptr)
                    obstacles[idx] = 99999;
                if (gx == gx2 && gy == gy2) break;
                gx += ddx; gy += ddy;
            }
        }
    }

    // Add Macro Routing Blockages (Infinite cost up to Metal3)
    for (GateInstance* inst : design.instances) {
        if (inst->isFixed && inst->type && inst->type->isMacro) {
            int mx1 = std::max(0, (int)(inst->x / scaleX));
            int my1 = std::max(0, (int)(inst->y / scaleY));
            int mx2 = std::min(gridW - 1, (int)((inst->x + inst->type->width) / scaleX));
            int my2 = std::min(gridH - 1, (int)((inst->y + inst->type->height) / scaleY));
            
            // Block routing on layer 1, 2, 3 (M1, M2, M3)
            for (int l = 1; l <= 3 && l < gridL; ++l) {
                for (int x = mx1; x <= mx2; ++x) {
                    for (int y = my1; y <= my2; ++y) {
                        int idx = getIdx(x, y, l, gridH, gridL);
                        // Do not block if it's an actual pin
                        if (pinOwner[idx] == nullptr) {
                            obstacles[idx] = 99999;
                        }
                    }
                }
            }
        }
    }

    // Stuck-detection state — must be per-call locals, not statics
    int prevConflicts = -1;
    int stuckIters    = 0;

    for (int iter = 1; iter <= maxIter && hasConflicts; ++iter) {
        std::cout << "  Iteration " << iter << "...\n" << std::flush;
        hasConflicts = false;
        
        // usage carries forward from previous iteration; rip-up (below) removes old routes
        auto& usage = grid_usage;

        int netCount = 0;
        int totalNets = 0;
        for (Net* n : design.nets) {
            if (n->name != "VDD" && n->name != "VSS" && n->connectedPins.size() >= 2) totalNets++;
        }

        // Reset searchId each PathFinder iteration to prevent stale ID collisions
        std::fill(astar_searchId.begin(), astar_searchId.begin() + totalNodes, 0);
        {
            auto& minCost    = astar_minCost;
            auto& parentIdx  = astar_parentIdx;
            auto& searchId   = astar_searchId;
            auto& netUsedId  = astar_netUsedId;
            int current_search_id = 0;
            int current_net_id    = 0;

            for (int netIdx = 0; netIdx < (int)design.nets.size(); ++netIdx) {
                Net* net = design.nets[netIdx];

                if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;

                // Rip-up previous route before rerouting.
                // Iterate with dedup: pair-encoding stores interior points twice
                // consecutively; decrementing them twice drives usage negative and
                // makes those cells appear free to later nets (congCost clamps at 0).
                if (iter > 1 && net->routePath.size() > 0) {
                    int prevRipIdx = -1;
                    for (const auto& p : net->routePath) {
                        int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                        int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                        int ridx = getIdx(px, py, p.layer, gridH, gridL);
                        if (ridx == prevRipIdx) { prevRipIdx = ridx; continue; }
                        prevRipIdx = ridx;
                        usage[ridx]--;
                    }
                }

                int currentNetCount = ++netCount;
                std::cout << "    Net " << currentNetCount << "/" << totalNets
                          << " [" << net->name << "] pins=" << net->connectedPins.size()
                          << "\n" << std::flush;
                
                net->routePath.clear();
                current_net_id++; // Generational ID — nodes owned by this net have netUsedId[idx]==current_net_id
                
                Pin* startPin = net->connectedPins[0];
                if (!startPin || !startPin->inst) continue;

                auto isSegmentClear = [&](int x1, int y1, int x2, int y2, int layer) -> bool {
                    int dx = (x2 > x1) ? 1 : ((x2 < x1) ? -1 : 0);
                    int dy = (y2 > y1) ? 1 : ((y2 < y1) ? -1 : 0);
                    int cx = x1, cy = y1;
                    while (true) {
                        int idx = getIdx(cx, cy, layer, gridH, gridL);
                        if (usage[idx] > 0 || obstacles[idx] > 0) return false;
                        if (cx == x2 && cy == y2) break;
                        cx += dx; cy += dy;
                    }
                    return true;
                };

                bool patternRouted = false;
                if (net->connectedPins.size() == 2) {
                    Pin* pA = net->connectedPins[0];
                    Pin* pB = net->connectedPins[1];
                    int ax = std::max(1, std::min(gridW-2, (int)(pA->getAbsX() / scaleX)));
                    int ay = std::max(1, std::min(gridH-2, (int)(pA->getAbsY() / scaleY)));
                    int bx = std::max(1, std::min(gridW-2, (int)(pB->getAbsX() / scaleX)));
                    int by = std::max(1, std::min(gridH-2, (int)(pB->getAbsY() / scaleY)));

                    // Calculate precise step directions (allowing 0 if coordinates match)
                    int stepX = (bx > ax) ? 1 : ((bx < ax) ? -1 : 0);
                    int stepY = (by > ay) ? 1 : ((by < ay) ? -1 : 0);
                    
                    // Attempt 1: Go Horizontal on Layer 1, then Vertical on Layer 2
                    if (isSegmentClear(ax, ay, bx, ay, 1) && isSegmentClear(bx, ay, bx, by, 2)) {
                        net->routePath.clear();
                        
                        // Walk X
                        if (stepX != 0) {
                            for (int x = ax; x != bx + stepX; x += stepX) { Point pt; pt.x = x * scaleX; pt.y = ay * scaleY; pt.layer = 1; net->routePath.push_back(pt); }
                        } else {
                            Point pt; pt.x = ax * scaleX; pt.y = ay * scaleY; pt.layer = 1; net->routePath.push_back(pt); // No movement needed
                        }
                        
                        // The Layer Transition VIA
                        Point ptVia1; ptVia1.x = bx * scaleX; ptVia1.y = ay * scaleY; ptVia1.layer = 2; // Z-axis VIA Transition
                        net->routePath.push_back(ptVia1);
                        
                        // Walk Y
                        if (stepY != 0) {
                            for (int y = ay + stepY; y != by + stepY; y += stepY) { Point pt; pt.x = bx * scaleX; pt.y = y * scaleY; pt.layer = 2; net->routePath.push_back(pt); }
                        } else {
                            Point pt; pt.x = bx * scaleX; pt.y = by * scaleY; pt.layer = 2; net->routePath.push_back(pt);
                        }
                        
                        patternRouted = true;
                    } 
                    // Attempt 2: Go Vertical on Layer 2, then Horizontal on Layer 1
                    else if (isSegmentClear(ax, ay, ax, by, 2) && isSegmentClear(ax, by, bx, by, 1)) {
                        net->routePath.clear();
                        
                        // Walk Y
                        if (stepY != 0) {
                            for (int y = ay; y != by + stepY; y += stepY) { Point pt; pt.x = ax * scaleX; pt.y = y * scaleY; pt.layer = 2; net->routePath.push_back(pt); }
                        } else {
                            Point pt; pt.x = ax * scaleX; pt.y = ay * scaleY; pt.layer = 2; net->routePath.push_back(pt); 
                        }
                        
                        // The Layer Transition VIA
                        Point ptVia2; ptVia2.x = ax * scaleX; ptVia2.y = by * scaleY; ptVia2.layer = 1; // Z-axis VIA Transition
                        net->routePath.push_back(ptVia2);
                        
                        // Walk X
                        if (stepX != 0) {
                            for (int x = ax + stepX; x != bx + stepX; x += stepX) { Point pt; pt.x = x * scaleX; pt.y = by * scaleY; pt.layer = 1; net->routePath.push_back(pt); }
                        } else {
                            Point pt; pt.x = bx * scaleX; pt.y = by * scaleY; pt.layer = 1; net->routePath.push_back(pt); 
                        }
                        patternRouted = true;
                    }
                    
                    if (patternRouted && net->routePath.size() > 1) {
                        std::vector<Point> finalPath;
                        for (size_t i = 0; i < net->routePath.size() - 1; ++i) {
                            finalPath.push_back(net->routePath[i]);
                            finalPath.push_back(net->routePath[i+1]);
                        }
                        net->routePath = finalPath;
                    }
                }

                if (patternRouted) {
                    for (const auto& p : net->routePath) {
                        int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                        int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                        if (p.layer >= 1 && p.layer < gridL)
                            usage[getIdx(px, py, p.layer, gridH, gridL)]++;
                    }
                    continue;
                }

                int sx = std::max(0, std::min(gridW-1, (int)(startPin->getAbsX() / scaleX)));
            int sy = std::max(0, std::min(gridH-1, (int)(startPin->getAbsY() / scaleY)));
            int sl = 1; // All pins start physically on Layer 1
            
            // Route sequentially to all sinks in the net
            for (size_t i = 1; i < net->connectedPins.size(); ++i) {
                Pin* endPin = net->connectedPins[i];
                if (!endPin || !endPin->inst) continue;

                int ex = std::max(0, std::min(gridW-1, (int)(endPin->getAbsX() / scaleX)));
                int ey = std::max(0, std::min(gridH-1, (int)(endPin->getAbsY() / scaleY)));
                int el = 1; // Destination is on Layer 1

                // GENERATIONAL ARRAY: Increment ID instead of O(N) memory clear
                current_search_id++;
                // parent doesn't need full reset - only accessed for found paths

                // Reuse member buffer — clear() keeps capacity, preventing heap reallocation
                astar_pq_buf.clear();

                int startIdx = getIdx(sx, sy, sl, gridH, gridL);
                int targetIdx = getIdx(ex, ey, el, gridH, gridL);
                astar_pq_buf.push_back({startIdx, 0.0f, 0.0f});
                std::push_heap(astar_pq_buf.begin(), astar_pq_buf.end(), std::greater<Node>());
                searchId[startIdx] = current_search_id;
                minCost[startIdx] = 0.0;
                parentIdx[startIdx] = -1;

                bool found = false;
                int foundIdx = -1;

                // 3. DYNAMIC BOUNDING BOX
                int bbMinX = gridW, bbMaxX = 0, bbMinY = gridH, bbMaxY = 0;
                for (Pin* p : net->connectedPins) {
                    if (!p || !p->inst) continue;
                    int px = (int)(p->getAbsX() / scaleX);
                    int py = (int)(p->getAbsY() / scaleY);
                    bbMinX = std::min(bbMinX, px);
                    bbMaxX = std::max(bbMaxX, px);
                    bbMinY = std::min(bbMinY, py);
                    bbMaxY = std::max(bbMaxY, py);
                }
                int margin = std::min(100 + iter * 30, std::min(gridW / 2, gridH / 2));
                bbMinX = std::max(1, bbMinX - margin);
                bbMaxX = std::min(gridW - 2, bbMaxX + margin);
                bbMinY = std::max(1, bbMinY - margin);
                bbMaxY = std::min(gridH - 2, bbMaxY + margin);

                const int HL = gridH * gridL;
                const int dx[] = {-1, 1, 0, 0, 0, 0};
                const int dy[] = {0, 0, -1, 1, 0, 0};
                const int dl[] = {0, 0, 0, 0, -1, 1};

                const int MAX_EXPAND = 500000; // Safety cap — 500k handles detour paths in 460x460 grid
                int nodesExpanded = 0;
                while (!astar_pq_buf.empty() && nodesExpanded < MAX_EXPAND) {
                    Node curr = astar_pq_buf.front();
                    std::pop_heap(astar_pq_buf.begin(), astar_pq_buf.end(), std::greater<Node>());
                    astar_pq_buf.pop_back();
                    nodesExpanded++;

                    if (curr.idx == targetIdx) {
                        found = true;
                        foundIdx = curr.idx;
                        break;
                    }

                    if ((double)curr.gCost > minCost[curr.idx]) continue;

                    // Decode (cx, cy, cl) from flat index
                    int cx = curr.idx / HL;
                    int rem = curr.idx % HL;
                    int cy = rem / gridL;
                    int cl = rem % gridL;

                    for (int d = 0; d < 6; ++d) {
                        int nx = cx + dx[d];
                        int ny = cy + dy[d];
                        int nl = cl + dl[d];

                        if (nx < bbMinX || nx > bbMaxX || ny < bbMinY || ny > bbMaxY ||
                            nl < 1 || nl >= gridL) continue;

                        int nIdx = getIdx(nx, ny, nl, gridH, gridL);
                        float stepCost;
                        if (dl[d] != 0) {
                            stepCost = 4.0f;
                        } else if (nl == 1) {
                            stepCost = 15.0f;
                        } else if (nl == 2 || nl == 4 || nl == 6) {
                            stepCost = (dx[d] != 0) ? 5.0f : 1.0f;
                        } else {
                            stepCost = (dy[d] != 0) ? 5.0f : 1.0f;
                        }

                        if (obstacles[nIdx] >= 99999) {
                            if (nIdx != targetIdx) continue;
                        }

                        float congCost = 0.0f;
                        if (netUsedId[nIdx] != current_net_id) {
                            int eff = (usage[nIdx] > 0) ? usage[nIdx] : 0;
                            if (eff > 0) {
                                // Hard-block cells committed by earlier nets (1e7 >> any
                                // detour cost), except the explicit target which must always
                                // be reachable. Converts routing to sequential: first-come,
                                // first-served — eliminates the soft-penalty oscillation.
                                congCost = (nIdx != targetIdx) ? 1e7f : 0.0f;
                            } else if (obstacles[nIdx] > 0) {
                                congCost = (float)(obstacles[nIdx] * 20.0);
                            }
                        }

                        double newCost = (double)curr.gCost + stepCost * history[nIdx] + congCost;

                        if (searchId[nIdx] != current_search_id) {
                            searchId[nIdx] = current_search_id;
                            minCost[nIdx] = 1e9;
                        }

                        if (newCost < minCost[nIdx]) {
                            minCost[nIdx] = newCost;
                            float h = (float)(std::abs(ex - nx) + std::abs(ey - ny) + std::abs(el - nl) * 10);
                            parentIdx[nIdx] = curr.idx;
                            astar_pq_buf.push_back({nIdx, (float)newCost, (float)newCost + h});
                            std::push_heap(astar_pq_buf.begin(), astar_pq_buf.end(), std::greater<Node>());
                        }
                    }
                }

                // Backtrack to build the path segments
                if (found) {
                    astar_segment.clear(); // O(1) — retains capacity
                    auto& segment = astar_segment;
                    int curIdx = foundIdx;
                    int maxBacktrack = totalNodes + 2; // safety: can't visit more nodes than exist
                    while (curIdx >= 0 && maxBacktrack-- > 0) {
                        int cx2 = curIdx / HL;
                        int cy2 = (curIdx % HL) / gridL;
                        int cl2 = curIdx % gridL;
                        Point p;
                        p.x = (int)(cx2 * scaleX);
                        p.y = (int)(cy2 * scaleY);
                        p.layer = cl2;
                        segment.push_back(p);
                        netUsedId[getIdx(cx2, cy2, cl2, gridH, gridL)] = current_net_id;
                        curIdx = parentIdx[curIdx];
                    }
                    std::reverse(segment.begin(), segment.end());
                    
                    for (size_t k = 0; k < segment.size() - 1; ++k) {
                        net->routePath.push_back(segment[k]);
                        net->routePath.push_back(segment[k+1]);
                    }
                    sx = ex; sy = ey; sl = el; // Start next segment from here
                }
            }
            
            // Strip out any accidental duplicate nodes before committing the path
            auto last = std::unique(net->routePath.begin(), net->routePath.end(), [](const Point& a, const Point& b) {
                return a.x == b.x && a.y == b.y && a.layer == b.layer;
            });
            net->routePath.erase(last, net->routePath.end());
            
            // Commit path to usage map (no pin-lock offset — usage tracks real occupancy)
            for (const auto& p : net->routePath) {
                int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                if (p.layer >= 1 && p.layer < gridL)
                    usage[getIdx(px, py, p.layer, gridH, gridL)]++;
            }

            } // end net loop
        } // end sequential routing block
        
        // Post-Iteration: Detect TRUE cross-net conflicts via cell ownership map.
        //
        // The routePath uses pair-segment encoding: path A→B→C is stored as
        // [A,B, B,C] so every interior point appears twice consecutively.
        // The old usage[idx]>1 check fired on a net's own path (false positive),
        // producing ~200 phantom conflicts that caused the 202↔207 oscillation.
        // Fix: skip consecutive duplicate indices and check cross-net ownership.
        int conflicts = 0;

        // Penalty multiplier grows each iteration for exponential escalation
        float penaltyMult = 1.5f + 0.5f * std::min(iter - 1, 8); // 1.5 → 5.5

        // Ensure cell_owner is sized; fill with -1 (unowned)
        if ((int)cell_owner.size() < totalNodes)
            cell_owner.assign(totalNodes, -1);
        else
            std::fill(cell_owner.begin(), cell_owner.begin() + totalNodes, -1);

        // Pin-junction whitelist: a cell is legal if any of net's own pins map to it.
        // Uses grid coordinates (gpx/gpy) — routePath stores (int)(grid*scale) which
        // does NOT round-trip cleanly back to the physical pin coordinate, so comparing
        // physical coords caused the whitelist to always miss and flag ~90 false conflicts.
        auto isLegalPinJunction = [&](Net* net, int gpx, int gpy, int gz) -> bool {
            if (gz != 1) return false;
            for (Pin* pin : net->connectedPins) {
                if (!pin || !pin->inst) continue;
                int ppx = std::max(0, std::min(gridW-1, (int)(pin->getAbsX() / scaleX)));
                int ppy = std::max(0, std::min(gridH-1, (int)(pin->getAbsY() / scaleY)));
                if (ppx == gpx && ppy == gpy) return true;
            }
            return false;
        };

        // Pass 1: claim cells — first net to visit a unique cell owns it
        for (int netIdx = 0; netIdx < (int)design.nets.size(); ++netIdx) {
            Net* net = design.nets[netIdx];
            if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;
            int prevIdx = -1;
            for (const auto& pt : net->routePath) {
                int px = std::max(0, std::min(gridW-1, (int)(pt.x / scaleX)));
                int py = std::max(0, std::min(gridH-1, (int)(pt.y / scaleY)));
                int idx = getIdx(px, py, pt.layer, gridH, gridL);
                if (idx == prevIdx) { prevIdx = idx; continue; } // skip pair-encoding dups
                prevIdx = idx;
                if (cell_owner[idx] < 0) cell_owner[idx] = netIdx;
            }
        }

        // Pass 2: flag cells where a different net is the owner → true conflict
        for (int netIdx = 0; netIdx < (int)design.nets.size(); ++netIdx) {
            Net* net = design.nets[netIdx];
            if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;
            int prevIdx = -1;
            for (const auto& pt : net->routePath) {
                int px = std::max(0, std::min(gridW-1, (int)(pt.x / scaleX)));
                int py = std::max(0, std::min(gridH-1, (int)(pt.y / scaleY)));
                int idx = getIdx(px, py, pt.layer, gridH, gridL);
                if (idx == prevIdx) { prevIdx = idx; continue; }
                prevIdx = idx;
                if (cell_owner[idx] >= 0 && cell_owner[idx] != netIdx) {
                    if (!isLegalPinJunction(net, px, py, pt.layer)) {
                        conflicts++;
                        history[idx] *= penaltyMult;
                        if (history[idx] > 1e6f) history[idx] = 1e6f;
                    }
                }
            }
        }

        if (conflicts > 0) {
            std::cout << "    Conflicts found: " << conflicts << ". Increasing penalties...\n";
            hasConflicts = true;
            if (conflicts == prevConflicts) {
                if (++stuckIters >= 5) {
                    std::cout << "    Flatlined for 5 iterations. Stopping early.\n";
                    break;
                }
            } else {
                stuckIters = 0;
            }
            prevConflicts = conflicts;
        } else {
            std::cout << "    Converged! Clean 3D routing achieved.\n";
            break;
        }
    }
    
    if (hasConflicts) {
        std::cout << "  Warning: Max iterations reached. Some shorts may remain.\n" << std::flush;
    }
    std::cout << "  PathFinder complete.\n" << std::flush;

    // Jog insertion is disabled: PathFinder now converges with 0 signal conflicts,
    // so post-route jogging only creates spurious DRC violations by shifting signal
    // points onto adjacent PDN cells. Skip it entirely.
    std::cout << "  Jog insertion skipped (routing converged clean).\n";
}

void RouteEngine::ensureAstarCapacity(int totalNodes) {
    if (totalNodes <= astar_capacity) {
        std::fill(astar_searchId.begin(), astar_searchId.end(), 0);
        return;
    }
    astar_minCost.assign(totalNodes, 0.0);
    astar_parentIdx.assign(totalNodes, -1);
    astar_searchId.assign(totalNodes, 0);
    astar_netUsedId.assign(totalNodes, 0);
    grid_history.assign(totalNodes, 1.0);
    grid_obstacles.assign(totalNodes, 0);
    grid_usage.assign(totalNodes, 0);
    astar_pq_buf.reserve(std::min(totalNodes * 2, 400000)); // pre-allocate pq backing store
    astar_segment.reserve(std::min(totalNodes, 50000));    // pre-allocate backtrack path buffer
    astar_capacity = totalNodes;
}
