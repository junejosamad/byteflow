#pragma once
#include <string>
#include <memory>

// Forward declarations to avoid deep includes
class Design;
class RouteEngine;
class CtsEngine;
struct GLFWwindow;
struct ImDrawList;
struct ImVec2;
struct CtsNode;

class GuiEngine {
public:
    GuiEngine();
    ~GuiEngine();

    bool init(int width, int height, const std::string& title);
    void run(Design* design, RouteEngine* routeEngine, CtsEngine* ctsEngine = nullptr);
    void shutdown();

private:
    GLFWwindow* window;
    
    // CAD Canvas State
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;

    void renderCanvas(Design* design, RouteEngine* routeEngine, CtsEngine* ctsEngine = nullptr);
    void renderCtsTree(std::shared_ptr<CtsNode> node, ImDrawList* draw_list, ImVec2 canvas_p0, float canvas_h, float zoom);
};
