#include "vulkan.hpp"
#include <cstdio>
#include <exception>
#include <vector>

static void rectangle(std::vector<LcVertex> &out, float x, float y, float r, float g) {
    LcVertex a{x, y, r, g, 0, 1}, b{x + 60, y, r, g, 0, 1};
    LcVertex c{x + 60, y + 60, r, g, 0, 1}, d{x, y + 60, r, g, 0, 1};
    out.insert(out.end(), {a, b, c, a, c, d});
}
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(320, 240, "LuaC orientation test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    int result = 0;
    try {
        VulkanRenderer renderer;
        renderer.initialize(window, "build");
        std::vector<LcVertex> vertices;
        rectangle(vertices, 20, 20, 1, 0);
        rectangle(vertices, 20, 160, 0, 1);
        std::vector<LcDraw> draws = {{0, 6, 0, 0, 320, 120, 0}, {6, 6, 0, 120, 320, 120, 0}};
        double until = glfwGetTime() + 20;
        while (!glfwWindowShouldClose(window) && glfwGetTime() < until) {
            glfwPollEvents();
            renderer.render(vertices, draws, 320, 240);
        }
        renderer.shutdown();
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
