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
    // Use 1:1 grid mapping (no scaling) to prevent pin aliasing
    gridW = gridW + 20;
    gridH = gridH + 20;
    double scaleX = 1.0;
    double scaleY = 1.0;
    int maxIter = 30;
    int gridL = 4;   // Layers 0, 1, 2, 3
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
                    obstacles[getIdx(gx, gy, pt.layer, gridH, gridL)] += 100;
                }
            }
        }
    }

    for (int iter = 1; iter <= maxIter && hasConflicts; ++iter) {
        std::cout << "  Iteration " << iter << "...\n";
        hasConflicts = false;
        
        // Present usage map for this iteration
        std::vector<int> usage(totalNodes, 0);
        
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

            #pragma omp for schedule(dynamic)
            for (int netIdx = 0; netIdx < (int)design.nets.size(); ++netIdx) {
                Net* net = design.nets[netIdx];

                // Skip power lines, they are handled geometrically by PDN generator
                if (net->name == "VDD" || net->name == "VSS" || net->connectedPins.size() < 2) continue;
                
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

                // RESET pre-allocated arrays (fast memset, no alloc/dealloc)
                std::fill(minCost.begin(), minCost.end(), 1e9);
                // parent doesn't need full reset - only accessed for found paths

                std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
                
                int startIdx = getIdx(sx, sy, sl, gridH, gridL);
                pq.push({sx, sy, sl, 0.0, 0.0, -1, -1, -1});
                minCost[startIdx] = 0.0;
                
                // THE CRITICAL FIX: Initialize the parent of the start node so it doesn't loop infinitely
                parent[startIdx] = {sx, sy, sl, 0.0, 0.0, -1, -1, -1};
                
                bool found = false;
                int fx = -1, fy = -1, fl = -1;

                // Bounding box for A*: restrict search to region around src/dst
                int pad = std::max(100, (std::abs(ex - sx) + std::abs(ey - sy)) / 2);
                int bbMinX = std::max(0, std::min(sx, ex) - pad);
                int bbMaxX = std::min(gridW - 1, std::max(sx, ex) + pad);
                int bbMinY = std::max(0, std::min(sy, ey) - pad);
                int bbMaxY = std::min(gridH - 1, std::max(sy, ey) + pad);

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

                        // Stay within bounding box AND valid layers, with a 1-unit boundary Keep-Out Margin
                        if (nx >= bbMinX && nx <= bbMaxX && ny >= bbMinY && ny <= bbMaxY && 
                            nx >= 1 && nx < gridW - 1 && ny >= 1 && ny < gridH - 1 && 
                            nl >= 1 && nl < gridL) {
                            int nIdx = getIdx(nx, ny, nl, gridH, gridL);
                            double stepCost = 1.0; 
                            
                            if (dl[d] != 0) {
                                // Switching layers (Via)
                                stepCost = 5.0; 
                            } else if (nl == 1) {
                                // LAYER 1 (M1): Very expensive local driveway
                                stepCost = 15.0; 
                            } else if (nl == 2) {
                                // LAYER 2 (M2): Vertical Highway
                                // If moving horizontally (dx != 0), apply a penalty
                                if (dx[d] != 0) stepCost = 5.0;
                                else stepCost = 1.0; 
                            } else if (nl == 3) {
                                // LAYER 3 (M3): Horizontal Highway
                                // If moving vertically (dy != 0), apply a penalty
                                if (dy[d] != 0) stepCost = 5.0;
                                else stepCost = 1.0; 
                            }
                            
                            // CONGESTION PENALTY: Avoid other nets and static obstacles
                            double congCost = 0.0;
                            if (netUsedNodes.find({nx,ny,nl}) == netUsedNodes.end()) {
                                if (usage[nIdx] > 0 || obstacles[nIdx] > 0) {
                                    congCost = (usage[nIdx] + obstacles[nIdx]) * 500.0;
                                }
                                // Pin Keep-Out Zone: Massively penalize if stepping on SOMEONE ELSE's pin
                                if (pinOwner[nIdx] != nullptr && pinOwner[nIdx] != net) {
                                    congCost += 25000.0; // Absolute wall
                                }
                            }
                            
                            double newCost = curr.gCost + (stepCost * history[nIdx]) + congCost;
                            
                            if (newCost < minCost[nIdx]) {
                                minCost[nIdx] = newCost;
                                // 3D Manhattan Heuristic
                                double h = std::abs(ex - nx) + std::abs(ey - ny) + std::abs(el - nl)*10.0;
                                parent[nIdx] = curr; // User snippet optimization here uses `curr` as parent instead of creating it manually!
                                pq.push({nx, ny, nl, newCost, newCost + h, curr.x, curr.y, curr.l});
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
        for (int x = 0; x < gridW; ++x) {
            for (int y = 0; y < gridH; ++y) {
                for (int l = 1; l < gridL; ++l) {
                    int idx = getIdx(x, y, l, gridH, gridL);
                    // An actual conflict = multiple routed nets overlap (usage > 1),
                    // OR a routed net overlaps a static obstacle (usage > 0 && obstacles > 0).
                    if (usage[idx] > 1 || (usage[idx] > 0 && obstacles[idx] > 0)) {
                        conflicts++;
                        // Add jitter to prevent symmetric oscillation
                        history[idx] += 50.0 + ((rand() % 100) / 10.0);
                    }
                }
            }
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
            
            // Register this net's ownership if cell is empty
            if (occupancy.count(key) == 0) {
                occupancy[key] = net;
            }
            
            if (occupancy.count(key) && occupancy[key] != net) {
                // Conflict! Try shifting this point to find empty space
                bool resolved = false;
                int dx[] = {0, 0, 1, -1, 1, -1, 1, -1};
                int dy[] = {1, -1, 0, 0, 1, 1, -1, -1};
                
                for (int dist = 1; dist <= 3 && !resolved; ++dist) {
                    for (int d = 0; d < 8 && !resolved; ++d) {
                        int nx = p.x + dx[d] * dist;
                        int ny = p.y + dy[d] * dist;
                        // 1-unit Keep-Out Margin for jog insertion
                        if (nx >= 1 && nx < gridW - 1 && ny >= 1 && ny < gridH - 1) {
                            auto newKey = std::make_tuple(nx, ny, p.layer);
                            if (occupancy.count(newKey) == 0) {
                                // Dynamic occupancy update: free old spot, claim new
                                p.x = nx;
                                p.y = ny;
                                occupancy[newKey] = net;
                                jogsInserted++;
                                resolved = true;
                            }
                        }
                    }
                }
            }
        }
    }
    
    std::cout << "  Inserted " << jogsInserted << " jogs to resolve Layer 1 conflicts.\n";
}
