#include "GuiEngine.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include "db/Design.h"
#include "route/RouteEngine.h"
#include "cts/CtsEngine.h"

GuiEngine::GuiEngine() : window(nullptr) {}

GuiEngine::~GuiEngine() { shutdown(); }

bool GuiEngine::init(int width, int height, const std::string& title) {
    if (!glfwInit()) return false;

    // GL 3.0 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) return false;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void GuiEngine::run(Design* design, RouteEngine* routeEngine, CtsEngine* ctsEngine) {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render our custom EDA Canvas
        renderCanvas(design, routeEngine, ctsEngine);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.05f, 0.07f, 0.12f, 1.00f); // Byteflow dark theme background
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
}

void GuiEngine::renderCanvas(Design* design, RouteEngine* routeEngine, CtsEngine* ctsEngine) {
    // Remove padding so the canvas fills the entire window seamlessly
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Byteflow Visualizer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetContentRegionAvail();

    // --- 1. Auto-Center on First Frame ---
    static bool first_frame = true;
    if (first_frame) {
        // Calculate a zoom level that fits the entire chip on screen with a 10% margin
        float fit_x = canvas_sz.x / (design->coreWidth * 1.1f);
        float fit_y = canvas_sz.y / (design->coreHeight * 1.1f);
        zoom = std::min(fit_x, fit_y);

        // Center the chip perfectly in the middle of the window
        panX = (canvas_sz.x - (design->coreWidth * zoom)) * 0.5f;
        panY = (canvas_sz.y - (design->coreHeight * zoom)) * 0.5f;
        first_frame = false;
    }

    // --- 2. Perfect Zoom-to-Mouse & Panning ---
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            panX += io.MouseDelta.x;
            panY += io.MouseDelta.y;
        }
        if (io.MouseWheel != 0.0f) {
            float zoom_speed = 1.15f; // 15% zoom per scroll click
            float old_zoom = zoom;
            
            if (io.MouseWheel > 0) zoom *= zoom_speed;
            else zoom /= zoom_speed;
            
            if (zoom < 0.05f) zoom = 0.05f; // Prevent zooming into infinity

            // Math to keep the exact logical point under the mouse cursor stable
            ImVec2 mouse_pos = io.MousePos;
            panX = mouse_pos.x - (mouse_pos.x - panX) * (zoom / old_zoom);
            panY = mouse_pos.y - (mouse_pos.y - panY) * (zoom / old_zoom);
        }
    }

    // --- 3. Coordinate Helper (The Magic Inverter) ---
    // Standard EDA: (0,0) is Bottom-Left. ImGui: (0,0) is Top-Left.
    auto toScreen = [&](float lx, float ly) -> ImVec2 {
        float sx = canvas_p0.x + panX + (lx * zoom);
        float sy = canvas_p0.y + panY + ((design->coreHeight - ly) * zoom); // Inverted Y!
        return ImVec2(sx, sy);
    };

    // --- 4. Draw Core Boundary ---
    ImVec2 bl = toScreen(0, 0); // Bottom Left
    ImVec2 tr = toScreen(design->coreWidth, design->coreHeight); // Top Right
    
    draw_list->AddRectFilled(ImVec2(bl.x, tr.y), ImVec2(tr.x, bl.y), IM_COL32(15, 20, 30, 255)); 
    draw_list->AddRect(ImVec2(bl.x, tr.y), ImVec2(tr.x, bl.y), IM_COL32(100, 100, 100, 255), 0.0f, 0, 2.0f);

    // --- 5. Draw Standard Cells ---
    for (auto* inst : design->instances) {
        ImVec2 cell_bl = toScreen(inst->x, inst->y);
        ImVec2 cell_tr = toScreen(inst->x + inst->type->width, inst->y + inst->type->height);

        // Frustum Culling: Skip cells completely off-screen
        if (cell_tr.x < canvas_p0.x || cell_bl.x > canvas_p0.x + canvas_sz.x ||
            cell_bl.y < canvas_p0.y || cell_tr.y > canvas_p0.y + canvas_sz.y) {
            continue;
        }

        draw_list->AddRectFilled(ImVec2(cell_bl.x, cell_tr.y), ImVec2(cell_tr.x, cell_bl.y), IM_COL32(30, 64, 175, 180)); 
        draw_list->AddRect(ImVec2(cell_bl.x, cell_tr.y), ImVec2(cell_tr.x, cell_bl.y), IM_COL32(96, 165, 250, 200)); 
    }

    // --- 6. Draw Routed Wires ---
    // Dynamic thickness: Very thin when zoomed out, normal when zoomed in
    float baseThickness = std::max(1.0f, 0.5f * zoom); 

    for (auto* net : design->nets) {
        if (net->routePath.empty()) continue;
        
        size_t i = 0;
        while (i < net->routePath.size() - 1) {
            auto& start_pt = net->routePath[i];
            size_t j = i + 1;
            
            // Calculate initial direction
            int dx = net->routePath[j].x - start_pt.x;
            int dy = net->routePath[j].y - start_pt.y;
            dx = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
            dy = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

            // FIX: Disconnected PDN segments (VSS/VDD macro grids)
            // If the "segment" is jumping diagonally across the chip, it's NOT a real wire!
            // It's the GuiEngine traversing from the end of one stripe to the start of the next.
            if ((dx != 0 && dy != 0) && (net->name == "VDD" || net->name == "VSS")) {
                i++; // Skip rendering this jump, just move to the next valid start point
                continue;
            }

            // Look-ahead loop: Keep extending 'j' as long as the line is perfectly straight
            while (j < net->routePath.size() - 1) {
                auto& next_pt = net->routePath[j+1];
                
                int next_dx = next_pt.x - net->routePath[j].x;
                int next_dy = next_pt.y - net->routePath[j].y;
                next_dx = (next_dx > 0) ? 1 : (next_dx < 0 ? -1 : 0);
                next_dy = (next_dy > 0) ? 1 : (next_dy < 0 ? -1 : 0);

                // If direction is identical and layer is identical, extend the line!
                if (next_dx == dx && next_dy == dy && next_pt.layer == start_pt.layer) {
                    j++; 
                } else {
                    break; // The wire bent or changed layers!
                }
            }

            auto& end_pt = net->routePath[j];

            ImVec2 scr_p1 = toScreen(start_pt.x, start_pt.y);
            ImVec2 scr_p2 = toScreen(end_pt.x, end_pt.y);

            // Handle VIA (Zero-length logical line due to layer change)
            if (std::abs(scr_p1.x - scr_p2.x) < 0.1f && std::abs(scr_p1.y - scr_p2.y) < 0.1f) {
                if (start_pt.layer != end_pt.layer && zoom > 2.0f) {
                    draw_list->AddRectFilled(
                        ImVec2(scr_p2.x - baseThickness, scr_p2.y - baseThickness),
                        ImVec2(scr_p2.x + baseThickness, scr_p2.y + baseThickness),
                        IM_COL32(255, 255, 255, 255) // White Square Via
                    );
                }
                i = j; // Jump forward
                continue; 
            }

            // Draw ONE massive straight line instead of 500 tiny ones
            ImU32 wireColor;
            if (start_pt.layer == 1) wireColor = IM_COL32(239, 68, 68, 220);       // M1: Red
            else if (start_pt.layer == 2) wireColor = IM_COL32(34, 197, 94, 220); // M2: Green
            else wireColor = IM_COL32(6, 182, 212, 220);                          // M3: Cyan

            draw_list->AddLine(scr_p1, scr_p2, wireColor, baseThickness);
            
            // Draw VIA if moving into a layer change
            if (start_pt.layer != end_pt.layer && zoom > 2.0f) {
                draw_list->AddRectFilled(
                    ImVec2(scr_p2.x - baseThickness, scr_p2.y - baseThickness),
                    ImVec2(scr_p2.x + baseThickness, scr_p2.y + baseThickness),
                    IM_COL32(255, 255, 255, 255)
                );
            }
            
            i = j; // Skip the outer loop directly to the end of the straight line
        }
    }

    // --- 7. Draw the Clock Tree (If it exists) ---
    if (ctsEngine && ctsEngine->getClockTreeRoot()) {
        renderCtsTree(ctsEngine->getClockTreeRoot(), draw_list, canvas_p0, design->coreHeight, zoom);
    }

    ImGui::End();
    ImGui::PopStyleVar(); // Pop the window padding
}

void GuiEngine::shutdown() {
    if (window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }
}

void GuiEngine::renderCtsTree(std::shared_ptr<CtsNode> node, ImDrawList* draw_list, ImVec2 canvas_p0, float canvas_h, float zoom) {
    if (!node) return;

    // Standard CAD Y-Inversion (Match your existing toScreen logic)
    auto toScreen = [&](float lx, float ly) -> ImVec2 {
        float sx = canvas_p0.x + panX + (lx * zoom);
        float sy = canvas_p0.y + panY + ((canvas_h - ly) * zoom);
        return ImVec2(sx, sy);
    };

    ImVec2 p1 = toScreen(node->x, node->y);
    float nodeSize = std::max(3.0f, 2.0f * zoom);

    // Draw the Branch Point (Yellow Circle) or Sink (Cyan Circle)
    if (node->isLeaf) {
        draw_list->AddCircleFilled(p1, nodeSize, IM_COL32(0, 255, 255, 255)); // Cyan Leaf
    } else {
        draw_list->AddCircleFilled(p1, nodeSize, IM_COL32(255, 255, 0, 255)); // Yellow Branch
    }

    // Recursively draw lines to children
    float lineThickness = std::max(1.0f, 1.0f * zoom);
    if (node->left) {
        ImVec2 p2 = toScreen(node->left->x, node->left->y);
        draw_list->AddLine(p1, p2, IM_COL32(255, 0, 255, 200), lineThickness); // Magenta Line
        renderCtsTree(node->left, draw_list, canvas_p0, canvas_h, zoom);
    }
    if (node->right) {
        ImVec2 p2 = toScreen(node->right->x, node->right->y);
        draw_list->AddLine(p1, p2, IM_COL32(255, 0, 255, 200), lineThickness); // Magenta Line
        renderCtsTree(node->right, draw_list, canvas_p0, canvas_h, zoom);
    }
}
