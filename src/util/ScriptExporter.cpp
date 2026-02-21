#include "util/ScriptExporter.h"
#include <fstream>
#include <iostream>

void ScriptExporter::write(Design& design, std::string filename) {
    std::cout << "\n=== EXPORTING PYTHON SCRIPT ===\n";
    std::ofstream file(filename);

    if (!file.is_open()) {
        std::cout << "  Error: Could not create file " << filename << "\n";
        return;
    }

    // 1. Python Header & Setup
    file << "# OpenEDA Generated Layout Script\n";
    file << "import pya\n\n";
    
    // Robust Window Creation (Fire and Forget)
    file << "mw = pya.Application.instance().main_window()\n";
    file << "mw.create_layout(0)\n";            // Create new tab
    file << "view = mw.current_view()\n";       // Grab the active tab
    file << "cv = view.active_cellview()\n";    // Grab the cell container
    
    // Create Layout Object linked to the view
    file << "layout = cv.layout()\n"; 
    file << "layout.dbu = 0.001 # 1 unit = 1nm\n\n";

    // 2. Create Top Cell
    file << "top = layout.create_cell(\"" << design.name << "\")\n";

    // 3. Define Layers
    file << "# Layers\n";
    file << "l_m1 = layout.layer(1, 0)  # Metal1 (Blue) - Signal/Power Rails\n";
    file << "l_m2 = layout.layer(2, 0)  # Metal2 (Red) - Power Stripes\n";
    file << "l_die = layout.layer(235, 0) # Die Boundary\n\n";

    // 4. Draw Die Boundary
    file << "# Die Area\n";
    file << "top.shapes(l_die).insert(pya.Box(0, 0, 100000, 100000))\n\n";

    // 5. Draw Standard Cells (Gates)
    file << "# Gates\n";
    for (GateInstance* inst : design.instances) {
        int x = (int)(inst->x * 1000);
        int y = (int)(inst->y * 1000);
        int w = (int)(inst->type->width * 1000);
        int h = (int)(inst->type->height * 1000);

        // Draw Cell Body (Blue Box)
        file << "top.shapes(l_m1).insert(pya.Box(" 
             << x << ", " << y << ", " 
             << x + w << ", " << y + h << "))\n";
             
        // Draw Text Label
        file << "top.shapes(l_m1).insert(pya.Text(\"" 
             << inst->name << "\", " << x << ", " << y << "))\n";
    }
    file << "\n";

    // 6. Draw Wires & Power Grid
    file << "# Routing (Signals + PDN)\n";
    for (Net* net : design.nets) {
        if (net->routePath.empty()) continue;
        
        // 100nm width for wires, thicker for Power (optional logic)
        int width = 100; 
        if (net->name == "VDD" || net->name == "VSS") width = 400; // Thicker power lines

        for (size_t i = 0; i < net->routePath.size(); i += 2) {
            // Safety check for pairs
            if (i + 1 >= net->routePath.size()) break;
            
            auto p1 = net->routePath[i];
            auto p2 = net->routePath[i+1];

            int x1 = p1.x * 1000; int y1 = p1.y * 1000;
            int x2 = p2.x * 1000; int y2 = p2.y * 1000;
            
            std::string layer = (p1.layer == 1) ? "l_m1" : "l_m2";
            
            file << "top.shapes(" << layer << ").insert(pya.Path([pya.Point(" 
                 << x1 << ", " << y1 << "), pya.Point(" 
                 << x2 << ", " << y2 << ")], " << width << "))\n";

            // Draw Via if layers switch
            if (p1.layer != p2.layer) {
                 file << "top.shapes(l_m1).insert(pya.Box(" 
                     << x1 - 50 << ", " << y1 - 50 << ", " 
                     << x1 + 50 << ", " << y1 + 50 << "))\n";
                 file << "top.shapes(l_m2).insert(pya.Box(" 
                     << x1 - 50 << ", " << y1 - 50 << ", " 
                     << x1 + 50 << ", " << y1 + 50 << "))\n";
            }
        }
    }

    // 7. Finalize View
    file << "\n# Finalize View\n";
    file << "cv.cell = top\n";           // Set the active cell
    file << "view.add_missing_layers()\n"; // Auto-add layers to panel
    file << "view.zoom_fit()\n";         // Zoom to see the chip

    file << "print(\"OpenEDA Design Loaded Successfully!\")\n";
    
    file.close();
    std::cout << "  Successfully wrote script to " << filename << "\n";
}
