#include "route/RouteEngine.h"
#include <omp.h>
#include <iostream>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <set>
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

// A* Search Node
struct Node {
    int x, y, l;
    double gCost; // Cost from start
    double fCost; // gCost + heuristic
    int px, py, pl; // Parent coordinates for backtracking
    
    bool operator>(const Node& other) const {
        return fCost > other.fCost;
    }
};

// Helper to flatten 3D coordinates into a 1D array index for extreme speed
inline int getIdx(int x, int y, int l, int gridH, int gridL) {
    return (x * gridH + y) * gridL + l;
}

void RouteEngine::runRouting(Design& design, int gridW, int gridH) {
    // Use 1:1 grid mapping (no scaling) but with a massive padding to ensure Edge cells have routing space
    gridW = gridW + 60;
    gridH = gridH + 60;
    double scaleX = 1.0;
    double scaleY = 1.0;
    int maxIter = 30;
    int gridL = 6;   // Layers 0, 1, 2, 3, 4, 5 (M1 through M6)
    int totalNodes = gridW * gridH * gridL;
    std::cout << "  Grid: " << gridW << " x " << gridH << " x " << gridL 
              << " (" << totalNodes << " nodes)"
              << ", Max Iterations: " << maxIter << "\n";

    // History cost (long-term memory of traffic jams)
    std::vector<double> history(totalNodes, 1.0);
        
    bool hasConflicts = true;

    // Pre-calculate static obstacles (Pins and pre-routed power nets)
    std::vector<int> obstacles(totalNodes, 0);
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
    
    // Add Pre-Routed Paths (VDD, VSS, etc.) to the obstacle map
    for (Net* const preNet : design.nets) {
        if (preNet->name == "VDD" || preNet->name == "VSS" || preNet->connectedPins.size() < 2) {
            for (const Point& pt : preNet->routePath) {
                int gx = (int)(pt.x / scaleX);
                int gy = (int)(pt.y / scaleY);
                if (gx >= 0 && gx < gridW && gy >= 0 && gy < gridH && pt.layer >= 1 && pt.layer < gridL) {
                    // Reduce penalty factor significantly. It's a hindrance, but not a solid wall since we can drop vias around it
                    obstacles[getIdx(gx, gy, pt.layer, gridH, gridL)] += 2; 
                }
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
            
            // Block routing on layer 0, 1, 2 (M1, M2, M3)
            for (int l = 0; l < 3 && l < gridL; ++l) {
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

    for (int iter = 1; iter <= maxIter && hasConflicts; ++iter) {
        std::cout << "  Iteration " << iter << "...\n";
        hasConflicts = false;
        
        // Present usage map for this iteration. 
        // We initialize it with the footprints of all nets from the PREVIOUS iteration to enable clean-net skipping!
        std::vector<int> usage(totalNodes, 0);
        if (iter > 1) {
            for (Net* net : design.nets) {
                if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;
                for (const auto& p : net->routePath) {
                    int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                    int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                    usage[getIdx(px, py, p.layer, gridH, gridL)]++;
                }
            }
        }
        
        int netCount = 0;
        int totalNets = 0;
        for (Net* n : design.nets) {
            if (n->name != "VDD" && n->name != "VSS" && n->connectedPins.size() >= 2) totalNets++;
        }

        #pragma omp parallel
        {
            // PRE-ALLOCATE A* arrays ONCE PER THREAD !
            std::vector<double> minCost(totalNodes);
            std::vector<Node> parent(totalNodes);
            std::vector<int> searchId(totalNodes, 0);
            int current_search_id = 0;

            #pragma omp for schedule(dynamic)
            for (int netIdx = 0; netIdx < (int)design.nets.size(); ++netIdx) {
                Net* net = design.nets[netIdx];

                // Skip power lines, they are handled geometrically by PDN generator
                if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;

                // --- 2. ACTIVE RIP-UP & REROUTE (No Skipping!) ---
                if (iter > 1 && net->routePath.size() > 0) {
                    // Always rip it up by completely removing its old footprint from usage
                    #pragma omp critical(usage_map_update)
                    {
                        for (const auto& p : net->routePath) {
                            int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                            int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                            usage[getIdx(px, py, p.layer, gridH, gridL)]--;
                        }
                    }
                }
                
                int currentNetCount;
                #pragma omp critical(net_count_update)
                {
                    currentNetCount = ++netCount;
                }
                
                if (currentNetCount % 100 == 0) {
                    #pragma omp critical(print_lock)
                    {
                        std::cout << "    Routing net " << currentNetCount << "/" << totalNets << "...\n";
                    }
                }
                
                net->routePath.clear();
                std::set<Point3D> netUsedNodes; // Prevent a multi-pin net from penalizing itself
                
                Pin* startPin = net->connectedPins[0];
                if (!startPin || !startPin->inst) continue;

                // --- 1. UNLOCK OWN PINS (Critical Section) ---
                // Only one thread can modify the global usage map at a time
                #pragma omp critical(usage_map_update)
                {
                    for (Pin* p : net->connectedPins) {
                        int px = std::max(0, std::min(gridW-1, (int)(p->getAbsX() / scaleX)));
                        int py = std::max(0, std::min(gridH-1, (int)(p->getAbsY() / scaleY)));
                        usage[getIdx(px, py, 1, gridH, gridL)] -= 50; 
                    }
                }

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
                    #pragma omp critical(usage_map_update)
                    {
                        std::set<Point3D> localUsed;
                        for (auto& p : net->routePath) {
                            int px = std::max(0, std::min(gridW-1, (int)(p.x / scaleX)));
                            int py = std::max(0, std::min(gridH-1, (int)(p.y / scaleY)));
                            localUsed.insert({px, py, p.layer});
                        }
                        for (auto& p : localUsed) {
                            usage[getIdx(p.x, p.y, p.l, gridH, gridL)]++;
                        }
                        for (Pin* p : net->connectedPins) {
                            int px = std::max(0, std::min(gridW-1, (int)(p->getAbsX() / scaleX)));
                            int py = std::max(0, std::min(gridH-1, (int)(p->getAbsY() / scaleY)));
                            usage[getIdx(px, py, 1, gridH, gridL)] += 50; 
                        }
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

                std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
                
                int startIdx = getIdx(sx, sy, sl, gridH, gridL);
                pq.push({sx, sy, sl, 0.0, 0.0, -1, -1, -1});
                searchId[startIdx] = current_search_id;
                minCost[startIdx] = 0.0;
                
                // THE CRITICAL FIX: Initialize the parent of the start node so it doesn't loop infinitely
                parent[startIdx] = {sx, sy, sl, 0.0, 0.0, -1, -1, -1};
                
                bool found = false;
                int fx = -1, fy = -1, fl = -1;

                // 3. DYNAMIC BOUNDING BOX - INCREASE MARGIN DRAMATICALLY
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
                
                // Allow the router to swing wiiide around PDN via meshes (massively expanded for macro avoidance)
                int margin = 200 + (iter * 100); 
                bbMinX = std::max(1, bbMinX - margin);
                bbMaxX = std::min(gridW - 2, bbMaxX + margin);
                bbMinY = std::max(1, bbMinY - margin);
                bbMaxY = std::min(gridH - 2, bbMaxY + margin);

                // 3D Movement: Left, Right, Down, Up, Layer Down, Layer Up
                int dx[] = {-1, 1, 0, 0, 0, 0};
                int dy[] = {0, 0, -1, 1, 0, 0};
                int dl[] = {0, 0, 0, 0, -1, 1};

                while (!pq.empty()) {
                    Node curr = pq.top();
                    pq.pop();

                    if (curr.x == ex && curr.y == ey && curr.l == el) {
                        found = true;
                        fx = curr.x; fy = curr.y; fl = curr.l;
                        break;
                    }

                    int currIdx = getIdx(curr.x, curr.y, curr.l, gridH, gridL);
                    if (curr.gCost > minCost[currIdx]) continue;

                    for (int d = 0; d < 6; ++d) {
                        int nx = curr.x + dx[d];
                        int ny = curr.y + dy[d];
                        int nl = curr.l + dl[d];

                        // Stay within bounding box AND valid layers, loosen boundary Keep-Out Margin so edge pins can escape
                        if (nx >= bbMinX && nx <= bbMaxX && ny >= bbMinY && ny <= bbMaxY && 
                            nx >= 0 && nx < gridW && ny >= 0 && ny < gridH && 
                            nl >= 1 && nl < gridL) {
                            int nIdx = getIdx(nx, ny, nl, gridH, gridL);
                            double stepCost = 1.0; 
                            
                            if (dl[d] != 0) {
                                // Switching layers (Via) - Make it cheaper to jump layers to bypass congestion
                                stepCost = 4.0; 
                            } else if (nl == 1) {
                                // LAYER 1 (M1): Very expensive local driveway
                                stepCost = 15.0; 
                            } else if (nl == 2 || nl == 4) {
                                // LAYER 2, 4 (M2, M4): Vertical Highways
                                // If moving horizontally (dx != 0), apply a penalty
                                if (dx[d] != 0) stepCost = 5.0;
                                else stepCost = 1.0; 
                            } else if (nl == 3 || nl == 5) {
                                // LAYER 3, 5 (M3, M5): Horizontal Highways
                                // If moving vertically (dy != 0), apply a penalty
                                if (dy[d] != 0) stepCost = 5.0;
                                else stepCost = 1.0; 
                            }
                            
                            // CONGESTION PENALTY: Avoid other nets and static obstacles
                            double congCost = 0.0;
                            if (netUsedNodes.find({nx,ny,nl}) == netUsedNodes.end()) {
                                if (usage[nIdx] > 0 || obstacles[nIdx] > 0) {
                                    congCost = (usage[nIdx] * 15000.0) + (obstacles[nIdx] * 50.0); // Severely penalize hitting dynamic nets, lightly penalize shadow of static nets
                                }
                                // Removed massive pin keep-out penalty. In hyper-dense blocks like our CTS buffer, pins are adjacent.
                            }
                            
                            double newCost = curr.gCost + (stepCost * history[nIdx]) + congCost;
                            
                            // Lazy initialization of minCost
                            if (searchId[nIdx] != current_search_id) {
                                searchId[nIdx] = current_search_id;
                                minCost[nIdx] = 1e9;
                            }
                            
                            if (newCost < minCost[nIdx]) {
                                minCost[nIdx] = newCost;
                                // --- 1. RELAX A* HEURISTIC (Dijkstra-like exploration) ---
                                // The dense PDN grid makes the Manhattan distance an unreliable heuristic.
                                // Reducing the multiplier forces the algorithm to explore sideways paths.
                                double h = std::abs(ex - nx) + std::abs(ey - ny) + std::abs(el - nl)*10.0;
                                parent[nIdx] = curr; 
                                pq.push({nx, ny, nl, newCost, newCost + (h * 1.0), curr.x, curr.y, curr.l});
                            }
                        }
                    }
                }

                // Backtrack to build the path segments
                if (found) {
                    std::vector<Point> segment; // Fixed Net::Point -> Point
                    int cx = fx, cy = fy, cl = fl;
                    while (cx != -1) {
                        Point p; 
                        p.x = (int)(cx * scaleX);  // Scale back to design coordinates
                        p.y = (int)(cy * scaleY);  // Scale back to design coordinates
                        p.layer = cl;
                        segment.push_back(p);
                        netUsedNodes.insert({cx, cy, cl}); // Mark this net's footprint (grid coords)
                        
                        int cIdx = getIdx(cx, cy, cl, gridH, gridL);
                        Node pNode = parent[cIdx];
                        cx = pNode.px; cy = pNode.py; cl = pNode.pl;
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
            
            // --- 3. RELOCK PINS & UPDATE USAGE (Critical Section) ---
            #pragma omp critical(usage_map_update)
            {
                // Lock pins back up
                for (Pin* p : net->connectedPins) {
                    int px = std::max(0, std::min(gridW-1, (int)(p->getAbsX() / scaleX)));
                    int py = std::max(0, std::min(gridH-1, (int)(p->getAbsY() / scaleY)));
                    usage[getIdx(px, py, 1, gridH, gridL)] += 50; 
                }
                
                // Add the new routed path to the global usage map
                for (auto const& pt : netUsedNodes) {
                    usage[getIdx(pt.x, pt.y, pt.l, gridH, gridL)]++;
                }
            }
            } // end pragma omp for
        } // end pragma omp parallel
        
        // Post-Iteration: Check for conflicts and update history map
        int conflicts = 0;
        float current_penalty_multiplier = (iter > 5) ? 1.5f : 1.0f;

        // --- PIN WHITELIST HELPER ---
        // Pins physically reside on Layer 1. Check if a coordinate is a legal pin for this net.
        auto isLegalPinJunction = [&](Net* net, int x, int y, int z) -> bool {
            if (z != 1) return false; // All pins are on Layer 1
            for (Pin* pin : net->connectedPins) {
                if (!pin || !pin->inst) continue;
                if ((int)pin->getAbsX() == x && (int)pin->getAbsY() == y) {
                    return true;
                }
            }
            return false;
        };

        for (Net* net : design.nets) {
            if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;

            int local_conflicts = 0;

            for (size_t i = 0; i < net->routePath.size(); ++i) {
                auto& pt = net->routePath[i];
                int px = std::max(0, std::min(gridW-1, (int)(pt.x / scaleX)));
                int py = std::max(0, std::min(gridH-1, (int)(pt.y / scaleY)));
                int idx = getIdx(px, py, pt.layer, gridH, gridL);

                // If the grid node has more than 1 net on it...
                if (usage[idx] > 1) {

                    // --- THE WHITELIST CHECK ---
                    // If this coordinate is exactly the origin or destination pin,
                    // it is legally allowed to share this space!
                    if (isLegalPinJunction(net, pt.x, pt.y, pt.layer)) {
                        continue; // Ignore! Do not flag as a conflict.
                    }

                    // Otherwise, it's a real mid-wire collision. Punish it.
                    local_conflicts++;

                    // Aggressive history scaling to force rip-up
                    history[idx] += current_penalty_multiplier * 2.5f;
                }
            }

            conflicts += local_conflicts;
        }
        
        if (conflicts > 0) {
            std::cout << "    Conflicts found: " << conflicts << ". Increasing penalties...\n";
            hasConflicts = true;
            // Early termination if stuck at same conflict count
            if (iter > 1) {
                static int prevConflicts = -1;
                static int stuckIters = 0;
                if (iter == 2) { prevConflicts = -1; stuckIters = 0; } // reset on fresh run
                if (conflicts == prevConflicts) {
                    stuckIters++;
                    if (stuckIters >= 5) {
                        std::cout << "    Flatlined for 5 iterations. Stopping early.\n";
                        break;
                    }
                } else {
                    stuckIters = 0;
                }
                prevConflicts = conflicts;
            }
        } else {
            std::cout << "    Converged! Clean 3D routing achieved.\n";
            break;
        }
    }
    
    if (hasConflicts) {
        std::cout << "  Warning: Max iterations reached. Some shorts may remain.\n";
    }

    // === DETAILED ROUTING: Jog Insertion Pass ===
    // Fixes remaining Layer 1 shorts by shifting one wire segment by ±1 unit
    std::cout << "\n=== DETAILED ROUTING (Jog Insertion) ===\n";
    
    // Build occupancy map: (x,y,layer) -> net name
    std::map<std::tuple<int,int,int>, Net*> occupancy;
    int jogsInserted = 0;
    
    // First pass: populate occupancy with all route points
    for (Net* net : design.nets) {
        for (size_t i = 0; i < net->routePath.size(); ++i) {
            const Point& p = net->routePath[i];
            auto key = std::make_tuple(p.x, p.y, p.layer);
            if (occupancy.count(key) == 0) {
                occupancy[key] = net;
            }
            
            // For power nets, fill in intermediate points between consecutive segment endpoints
            if ((net->name == "VDD" || net->name == "VSS") && i + 1 < net->routePath.size()) {
                const Point& p2 = net->routePath[i + 1];
                if (p.layer == p2.layer) {
                    int dx = (p2.x > p.x) ? 1 : (p2.x < p.x) ? -1 : 0;
                    int dy = (p2.y > p.y) ? 1 : (p2.y < p.y) ? -1 : 0;
                    // Only interpolate straight lines (H or V), skip same-point or diagonal
                    if ((dx == 0) != (dy == 0)) {
                        int cx = p.x + dx, cy = p.y + dy;
                        int maxSteps = std::abs(p2.x - p.x) + std::abs(p2.y - p.y);
                        int steps = 0;
                        while ((cx != p2.x || cy != p2.y) && steps < maxSteps) {
                            auto ikey = std::make_tuple(cx, cy, p.layer);
                            if (occupancy.count(ikey) == 0) {
                                occupancy[ikey] = net;
                            }
                            cx += dx; cy += dy;
                            steps++;
                        }
                    }
                }
            }
        }
    }
    
    // Second pass: find conflicts and jog one net's wire
    for (Net* net : design.nets) {
        if (net->name == "VDD" || net->name == "VSS") continue;
        
        for (size_t i = 0; i < net->routePath.size(); ++i) {
            Point& p = net->routePath[i];
            auto key = std::make_tuple(p.x, p.y, p.layer);
            
            // Re-check ownership
            if (occupancy.count(key) && occupancy[key] != net) {
                // TRUE CONFLICT! Two dynamic nets want this cell.
                bool resolved = false;
                
                // Extremely simple jog logic: attempt a tiny X/Y shift, or jump up a layer (Via drop)
                int dx[] = {1, 0, -1, 0, 0};
                int dy[] = {0, 1, 0, -1, 0};
                int dL[] = {0, 0, 0, 0, 1}; // The 5th option is Z+1
                
                for (int d = 0; d < 5 && !resolved; ++d) {
                    int nx = p.x + dx[d];
                    int ny = p.y + dy[d];
                    int nl = p.layer + dL[d];
                    
                    if (nx >= 0 && nx < gridW && ny >= 0 && ny < gridH && nl < gridL) {
                        auto newKey = std::make_tuple(nx, ny, nl);
                        
                        if (occupancy.count(newKey) == 0) {
                            occupancy.erase(key);
                            p.x = nx;
                            p.y = ny;
                            p.layer = nl;
                            occupancy[newKey] = net;
                            jogsInserted++;
                            resolved = true;
                        }
                    }
                }
            } else {
                // We successfully claimed this spot. Ensure it belongs to us in the global map
                occupancy[key] = net;
            }
        }
    }
    
    std::cout << "  Inserted " << jogsInserted << " jogs to resolve Layer 1 conflicts.\n";
}
