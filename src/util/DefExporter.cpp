#include "util/DefExporter.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>

void DefExporter::write(Design& design, std::string filename) {
    std::cout << "\n=== EXPORTING DEF (Industry Standard) ===\n";
    std::ofstream file(filename);

    if (!file.is_open()) {
        std::cout << "  Error: Could not create file " << filename << "\n";
        return;
    }

    // 1. DEF Header
    file << "VERSION 5.8 ;\n";
    file << "DIVIDERCHAR \"/\" ;\n";
    file << "BUSBITCHARS \"[]\" ;\n";
    file << "DESIGN " << design.name << " ;\n";
    file << "UNITS DISTANCE MICRONS 100 ;\n"; // 100 DB units = 1 micron

    // 2. Die Area (Assuming 100x100 grid, scaled by 100)
    file << "DIEAREA ( 0 0 ) ( 10000 10000 ) ;\n\n";

    // 3. Components (Gates)
    file << "COMPONENTS " << design.instances.size() << " ;\n";
    for (GateInstance* inst : design.instances) {
        // Syntax: - Name Model + PLACED ( x y ) N ;
        // We scale x,y by 100 to match UNITS
        int x_db = (int)(inst->x * 100);
        int y_db = (int)(inst->y * 100);

        file << "- " << inst->name << " " << inst->type->name << " \n";
        file << "  + PLACED ( " << x_db << " " << y_db << " ) N ;\n";
    }
    file << "END COMPONENTS\n\n";

    // 4. Nets & Routing
    // This is the complex part. We convert our path to DEF segments.
    file << "NETS " << design.nets.size() << " ;\n";
    for (Net* net : design.nets) {
        file << "- " << net->name << "\n";

        // Connectivity ( ( Inst Pin ) ( Inst Pin ) ... )
        for (Pin* p : net->connectedPins) {
            file << "  ( " << p->inst->name << " " << p->name << " )";
        }
        file << "\n";

        // Routing
        // Syntax: + ROUTED Metal1 ( x y ) ( x y ) ...
        if (!net->routePath.empty()) {
            file << "  + ROUTED ";

            int currentLayer = -1;

            for (size_t i = 0; i < net->routePath.size(); ++i) {
                Point p = net->routePath[i];
                int x_db = p.x * 100;
                int y_db = p.y * 100;
                int layer = p.layer; // 1 or 2

                if (layer != currentLayer) {
                    // Layer Change logic in DEF is complex (VIA definitions), 
                    // Simplified: We start a new segment for each layer
                    // Real DEF requires defined VIA definitions in header.
                    // For this simple demo, we essentially just list points 
                    // labeled by their metal layer.

                    if (i > 0) file << "\n    NEW "; // Break segment
                    file << (layer == 1 ? "Metal1" : "Metal2") << " ";
                    file << "( " << x_db << " " << y_db << " ) ";
                    currentLayer = layer;
                }
                else {
                    file << "( " << x_db << " " << y_db << " ) ";
                }

                // If this is a VIA location (layer change next), add *
                if (i < net->routePath.size() - 1) {
                    if (net->routePath[i + 1].layer != layer) {
                        file << "M1_M2_VIA ";
                    }
                }
            }
            file << ";\n";
        }
        else {
            file << ";\n"; // Unrouted net
        }
    }
    file << "END NETS\n\n";

    file << "END DESIGN\n";
    file.close();
    std::cout << "  Successfully wrote layout to " << filename << "\n";
}

void DefExporter::writeDEF(const std::string& filename, Design* design, double coreWidth, double coreHeight) {
    std::cout << "\n=== EXPORTING DEF (Industry Standard) ===\n";
    std::ofstream out(filename);
    
    if (!out.is_open()) {
        std::cerr << "  Error: Could not open " << filename << " for writing.\n";
        return;
    }

    int DBU = 1000; // 1000 Database Units per Micron

    // 1. DEF Header
    out << "VERSION 5.8 ;\n";
    out << "DIVIDERCHAR \"/\" ;\n";
    out << "BUSBITCHARS \"[]\" ;\n";
    out << "DESIGN " << design->name << " ;\n";
    out << "UNITS DISTANCE MICRONS " << DBU << " ;\n\n";

    // 2. Die Area
    out << "DIEAREA ( 0 0 ) ( " 
        << (int)(coreWidth * DBU) << " " 
        << (int)(coreHeight * DBU) << " ) ;\n\n";

    // 3. Components (Placed Instances)
    out << "COMPONENTS " << design->instances.size() << " ;\n";
    for (GateInstance* inst : design->instances) {
        out << "- " << inst->name << " " << inst->type->name 
            << " + PLACED ( " 
            << (int)(inst->x * DBU) << " " 
            << (int)(inst->y * DBU) << " ) N ;\n";
    }
    out << "END COMPONENTS\n\n";

    // 4. Nets (Routed Paths)
    // Filter out nets that don't have routing paths
    int routedNetsCount = 0;
    for (Net* net : design->nets) {
        if (!net->routePath.empty()) routedNetsCount++;
    }

    out << "NETS " << routedNetsCount << " ;\n";
    for (Net* net : design->nets) {
        if (net->routePath.empty()) continue;

        out << "- " << net->name << "\n";
        
        // List all connected pins
        for (Pin* pin : net->connectedPins) {
            out << "  ( " << pin->inst->name << " " << pin->name << " )\n";
        }

        // --- Deduplicate consecutive identical points ---
        std::vector<Point> deduped;
        for (size_t i = 0; i < net->routePath.size(); ++i) {
            if (deduped.empty() ||
                net->routePath[i].x != deduped.back().x ||
                net->routePath[i].y != deduped.back().y ||
                net->routePath[i].layer != deduped.back().layer) {
                deduped.push_back(net->routePath[i]);
            }
        }

        // Check if this is a power net (VDD/VSS) — they use pairs of points as segments
        bool isPowerNet = (net->name == "VDD" || net->name == "VSS");

        if (isPowerNet) {
            // Power nets: each pair of consecutive points is one wire segment
            // We emit each pair as a separate path statement
            bool first = true;
            for (size_t i = 0; i + 1 < deduped.size(); i += 2) {
                int layer = std::min(deduped[i].layer, 2); // Clamp to Metal1/Metal2
                std::string layerName = "Metal" + std::to_string(layer);

                if (first) {
                    out << "  + ROUTED " << layerName << " ( "
                        << deduped[i].x * DBU << " " << deduped[i].y * DBU << " ) ( "
                        << deduped[i+1].x * DBU << " " << deduped[i+1].y * DBU << " )\n";
                    first = false;
                } else {
                    out << "    NEW " << layerName << " ( "
                        << deduped[i].x * DBU << " " << deduped[i].y * DBU << " ) ( "
                        << deduped[i+1].x * DBU << " " << deduped[i+1].y * DBU << " )\n";
                }
            }
            out << "  ;\n";
        } else {
            // Signal nets: group consecutive points by layer into segments
            // Each segment must have >= 2 points, or 1 point + VIA
            
            // Build layer segments: each segment is a vector of points on the same layer
            struct Segment {
                int layer;
                std::vector<std::pair<int,int>> pts; // (x_dbu, y_dbu)
            };
            std::vector<Segment> segments;
            
            for (size_t i = 0; i < deduped.size(); ++i) {
                int layer = std::min(deduped[i].layer, 2);
                int px = deduped[i].x * DBU;
                int py = deduped[i].y * DBU;
                
                if (segments.empty() || segments.back().layer != layer) {
                    segments.push_back({layer, {}});
                }
                segments.back().pts.push_back({px, py});
            }
            
            // Output segments
            bool firstSeg = true;
            for (size_t s = 0; s < segments.size(); ++s) {
                auto& seg = segments[s];
                std::string layerName = "Metal" + std::to_string(seg.layer);
                
                if (firstSeg) {
                    out << "  + ROUTED " << layerName << " ";
                    firstSeg = false;
                } else {
                    out << "\n    NEW " << layerName << " ";
                }
                
                // Output all points in this segment
                for (auto& pt : seg.pts) {
                    out << "( " << pt.first << " " << pt.second << " ) ";
                }
                
                // If this is a single-point segment, append a VIA to connect to adjacent layer
                if (seg.pts.size() == 1) {
                    out << "M1_M2_VIA ";
                }
            }
            out << ";\n";
        }
    }
    out << "END NETS\n\n";

    out << "END DESIGN\n";
    out.close();
    std::cout << "  Successfully wrote layout to " << filename << "\n";
}
