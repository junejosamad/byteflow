#include "util/SvgExporter.h"
#include <iostream>

void SvgExporter::exportLayout(Design& design, std::string filename, double coreW, double coreH) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create SVG file " << filename << "\n";
        return;
    }

    std::cout << "Exporting Layout to " << filename << "...\n";

    // 1. UPDATE THE VIEWBOX TO match coreW + 50
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-20 -20 " << coreW + 50 << " " << coreH + 50 << "\" style=\"background-color:#1e1e1e;\">\n";

    // 2. UPDATE THE CHIP OUTLINE
    file << "  <rect x=\"0\" y=\"0\" width=\"" << coreW << "\" height=\"" << coreH << "\" fill=\"none\" stroke=\"#555\" stroke-width=\"2\"/>\n";

    // 3. UPDATE THE GRID LOOPS TO coreW/coreH
    for (int i = 0; i <= (int)coreW; i += 10) {
        // Vertical lines
        file << "  <line x1=\"" << i << "\" y1=\"0\" x2=\"" << i << "\" y2=\"" << coreH << "\" stroke=\"#333\" stroke-width=\"0.5\"/>\n";
    }
    for (int i = 0; i <= (int)coreH; i += 10) {
        // Horizontal lines
        file << "  <line x1=\"0\" y1=\"" << i << "\" x2=\"" << coreW << "\" y2=\"" << i << "\" stroke=\"#333\" stroke-width=\"0.5\"/>\n";
    }

    // 3. Draw Routed Wires (Manhattan Paths)
    for (Net* net : design.nets) {
        if (net->routePath.empty()) continue;

        for (size_t i = 0; i < net->routePath.size() - 1; ++i) {
            Point p1 = net->routePath[i];
            Point p2 = net->routePath[i + 1];

            std::string color = (p1.layer == 1) ? "blue" : "red";

            // Draw Segment (remove +20 offset as the grid is 0-indexed now)
            file << "  <line x1=\"" << p1.x << "\" y1=\"" << p1.y
                << "\" x2=\"" << p2.x << "\" y2=\"" << p2.y
                << "\" stroke=\"" << color << "\" stroke-width=\"1\" />\n";

            if (p1.layer != p2.layer) {
                file << "  <circle cx=\"" << p1.x << "\" cy=\"" << p1.y
                    << "\" r=\"1.5\" fill=\"black\" />\n";
            }
        }
    }

    // 4. Draw Gates (Instances)
    file << "  \n";
    for (GateInstance* inst : design.instances) {
        std::string color = "#4CAF50"; // Green for standard cells
        if (inst->type->isSequential) color = "#FF5722"; // Orange for DFFs

        file << "  <rect x=\"" << inst->x << "\" y=\"" << inst->y
            << "\" width=\"" << inst->type->width << "\" height=\"" << inst->type->height
            << "\" fill=\"" << color << "\" stroke=\"black\" stroke-width=\"0.5\" />\n";

        // Gate Name Label (Tiny text)
        file << "  <text x=\"" << inst->x << "\" y=\"" << inst->y - 2
            << "\" font-family=\"Arial\" font-size=\"3\" fill=\"#ccc\">"
            << inst->name << "</text>\n";
    }

    file << "</svg>\n";
    file.close();
}
