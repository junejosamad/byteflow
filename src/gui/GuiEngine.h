#pragma once
#include <string>

// Forward declarations to avoid deep includes
class Design;
class RouteEngine;
struct GLFWwindow;

class GuiEngine {
public:
    GuiEngine();
    ~GuiEngine();

    bool init(int width, int height, const std::string& title);
    void run(Design* design, RouteEngine* routeEngine);
    void shutdown();

private:
    GLFWwindow* window;
    
    // CAD Canvas State
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;

    void renderCanvas(Design* design, RouteEngine* routeEngine);
};
