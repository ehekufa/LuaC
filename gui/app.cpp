#include "vulkan.hpp"
#include "font8x8_basic.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {
struct Color { float r, g, b, a; };
constexpr Color text{0.82f, 0.88f, 0.96f, 1};
constexpr Color muted{0.42f, 0.51f, 0.64f, 1};
constexpr Color accent{0.22f, 0.82f, 0.65f, 1};
constexpr Color panel{0.065f, 0.085f, 0.125f, 1};
constexpr Color selection{0.12f, 0.20f, 0.28f, 1};

class Canvas {
public:
    std::vector<LcVertex> vertices;
    void rect(float x, float y, float w, float h, Color c) {
        LcVertex a{x,y,c.r,c.g,c.b,c.a}, b{x+w,y,c.r,c.g,c.b,c.a}, c1{x,y+h,c.r,c.g,c.b,c.a};
        LcVertex d{x+w,y,c.r,c.g,c.b,c.a}, e{x+w,y+h,c.r,c.g,c.b,c.a}, f{x,y+h,c.r,c.g,c.b,c.a};
        vertices.insert(vertices.end(), {a,b,c1,d,e,f});
    }
    void glyph(float x, float y, unsigned char ch, float scale, Color color) {
        if (ch >= 128) ch = '?';
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = static_cast<unsigned char>(font8x8_basic[ch][row]);
            for (int col = 0; col < 8; ++col)
                if (bits & (1u << col)) rect(x + col * scale, y + row * scale, scale, scale, color);
        }
    }
    void print(float x, float y, const std::string &value, Color color = text, float scale = 1, size_t maxChars = SIZE_MAX) {
        float start = x;
        size_t shown = 0;
        for (unsigned char ch : value) {
            if (ch == '\n') { x = start; y += 9 * scale; continue; }
            if (shown++ >= maxChars) break;
            glyph(x, y, ch, scale, color);
            x += 8 * scale;
        }
    }
};

struct App {
    GLFWwindow *window = nullptr;
    VulkanRenderer renderer;
    Canvas canvas;
    std::string source;
    std::string output = "Ready. F5 — run, F6 — syntax check, Ctrl+S — save.";
    std::string filename = "untitled.lua";
    std::string compiler;
    size_t cursor = 0;
    int width = 1280, height = 800;
    double mouseX = 0, mouseY = 0;
    bool dirty = false;

    bool hit(float x, float y, float w, float h) const { return mouseX >= x && mouseX <= x+w && mouseY >= y && mouseY <= y+h; }
    void load(const char *path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) { output = std::string("Cannot open: ") + path; return; }
        std::ostringstream bytes; bytes << file.rdbuf(); source = bytes.str();
        filename = path; cursor = source.size(); dirty = false; output = std::string("Opened ") + path;
    }
    void save() {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file) { output = "Save failed: " + filename; return; }
        file.write(source.data(), static_cast<std::streamsize>(source.size()));
        dirty = false; output = "Saved " + filename;
    }
    static std::string quote(const std::string &value) {
#ifdef _WIN32
        std::string result = "\"";
        for (char c : value) { if (c == '\"') result += '\\'; result += c; }
        return result + "\"";
#else
        std::string result = "'";
        for (char c : value) { if (c == '\'') result += "'\\''"; else result += c; }
        return result + "'";
#endif
    }
    void execute(const std::string &arguments) {
        std::string command = quote(compiler) + " " + arguments + " 2>&1";
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe) { output = std::string("Cannot start compiler: ") + std::strerror(errno); return; }
        char buffer[1024]; std::string log;
        while (fgets(buffer, sizeof(buffer), pipe)) log += buffer;
        int status = pclose(pipe);
        output = log.empty() ? (status == 0 ? "Done." : "Compiler failed.") : log;
    }
    void run() {
        if (dirty) save();
        execute("--jit " + quote(filename));
    }
    void check() {
        std::string temp = filename + ".check.lua";
        { std::ofstream file(temp, std::ios::binary | std::ios::trunc); file << source; }
        execute("--check " + quote(temp));
        std::remove(temp.c_str());
    }
    void button(float x, const char *label, bool active) {
        float w = 92;
        canvas.rect(x, 12, w, 34, active ? Color{0.16f,0.35f,0.31f,1} : selection);
        canvas.print(x + 12, 22, label, active ? accent : text, 1);
    }
    void draw() {
        canvas.vertices.clear();
        canvas.rect(0, 0, static_cast<float>(width), 58, panel);
        canvas.print(18, 13, "LuaC", accent, 2);
        canvas.print(18, 36, "VULKAN STUDIO", muted, 1);
        button(160, "Run  F5", true);
        button(262, "Check F6", false);
        button(364, "Save", false);
        canvas.print(478, 25, dirty ? filename + " *" : filename, muted, 1, 60);
        float split = width * 0.61f;
        canvas.rect(0, 58, split, height - 58.0f, Color{0.035f,0.045f,0.068f,1});
        canvas.rect(split, 58, width - split, height - 58.0f, Color{0.045f,0.055f,0.080f,1});
        canvas.rect(split, 58, 1, height - 58.0f, selection);
        canvas.print(18, 72, "SOURCE", accent, 1);
        canvas.print(split + 18, 72, "OUTPUT", accent, 1);
        size_t lineStart = 0;
        int line = 1;
        float y = 100;
        int lineHeight = 15;
        int maxLines = std::max(1, (height - 120) / lineHeight);
        int firstLine = 1;
        int cursorLine = 1;
        for (size_t i = 0; i < cursor && i < source.size(); ++i) if (source[i] == '\n') ++cursorLine;
        if (cursorLine > maxLines - 2) firstLine = cursorLine - maxLines + 3;
        for (size_t i = 0; i <= source.size() && line <= firstLine + maxLines; ++i) {
            if (i == source.size() || source[i] == '\n') {
                if (line >= firstLine) {
                    std::string number = std::to_string(line);
                    canvas.print(16, y, number, muted, 1);
                    std::string textLine = source.substr(lineStart, i - lineStart);
                    size_t maxChars = static_cast<size_t>((split - 76) / 8);
                    canvas.print(68, y, textLine, text, 1, maxChars);
                    if (cursor >= lineStart && cursor <= i && line == cursorLine) {
                        float cursorX = 68 + static_cast<float>(cursor - lineStart) * 8;
                        canvas.rect(cursorX, y - 1, 2, 11, accent);
                    }
                    y += lineHeight;
                }
                ++line; lineStart = i + 1;
            }
        }
        float outY = 100;
        size_t outStart = 0;
        int outLine = 0;
        size_t outputMax = static_cast<size_t>((width - split - 36) / 8);
        for (size_t i = 0; i <= output.size() && outY < height - 30; ++i) {
            if (i == output.size() || output[i] == '\n') {
                std::string row = output.substr(outStart, i - outStart);
                for (size_t offset = 0; offset < row.size() || offset == 0; offset += outputMax) {
                    canvas.print(split + 18, outY, row.substr(offset, outputMax), outLine == 0 ? accent : text, 1);
                    outY += 15;
                    if (outY >= height - 30 || offset + outputMax >= row.size()) break;
                }
                ++outLine; outStart = i + 1;
            }
        }
        canvas.print(18, height - 22, "F5 run   F6 check   Ctrl+S save   Click source to edit", muted, 1);
        renderer.render(canvas.vertices, width, height);
    }
};

App *app(GLFWwindow *window) { return static_cast<App *>(glfwGetWindowUserPointer(window)); }
void resize(GLFWwindow *window, int width, int height) { App *a = app(window); a->width = std::max(width, 640); a->height = std::max(height, 400); }
void characters(GLFWwindow *window, unsigned int codepoint) {
    App *a = app(window);
    if (codepoint < 32 || codepoint == 127) return;
    if (codepoint < 128) a->source.insert(a->source.begin() + static_cast<long>(a->cursor), static_cast<char>(codepoint));
    else a->source.insert(a->source.begin() + static_cast<long>(a->cursor), '?');
    ++a->cursor; a->dirty = true;
}
void key(GLFWwindow *window, int keyCode, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    App *a = app(window);
    if ((mods & GLFW_MOD_CONTROL) && keyCode == GLFW_KEY_S) { a->save(); return; }
    if (keyCode == GLFW_KEY_F5) { a->run(); return; }
    if (keyCode == GLFW_KEY_F6) { a->check(); return; }
    if (keyCode == GLFW_KEY_BACKSPACE && a->cursor) { a->source.erase(--a->cursor, 1); }
    else if (keyCode == GLFW_KEY_DELETE && a->cursor < a->source.size()) a->source.erase(a->cursor, 1);
    else if (keyCode == GLFW_KEY_LEFT && a->cursor) --a->cursor;
    else if (keyCode == GLFW_KEY_RIGHT && a->cursor < a->source.size()) ++a->cursor;
    else if (keyCode == GLFW_KEY_HOME) { while (a->cursor && a->source[a->cursor-1] != '\n') --a->cursor; }
    else if (keyCode == GLFW_KEY_END) { while (a->cursor < a->source.size() && a->source[a->cursor] != '\n') ++a->cursor; }
    else if (keyCode == GLFW_KEY_ENTER) { a->source.insert(a->cursor++, 1, '\n'); }
    else if (keyCode == GLFW_KEY_TAB) { a->source.insert(a->cursor, "  "); a->cursor += 2; }
    else return;
    a->dirty = true;
}
void mouseButton(GLFWwindow *window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
    App *a = app(window);
    if (a->hit(160,12,92,34)) { a->run(); return; }
    if (a->hit(262,12,92,34)) { a->check(); return; }
    if (a->hit(364,12,92,34)) { a->save(); return; }
    float split = a->width * 0.61f;
    if (a->mouseX >= 68 && a->mouseX < split && a->mouseY >= 100) {
        int wantedLine = 1 + static_cast<int>((a->mouseY - 100) / 15);
        size_t pos = 0; int line = 1;
        while (pos < a->source.size() && line < wantedLine) if (a->source[pos++] == '\n') ++line;
        size_t column = static_cast<size_t>((a->mouseX - 68) / 8);
        size_t start = pos;
        while (pos < a->source.size() && a->source[pos] != '\n' && pos - start < column) ++pos;
        a->cursor = pos;
    }
}
void mouseMove(GLFWwindow *window, double x, double y) { App *a = app(window); a->mouseX = x; a->mouseY = y; }
}

int main(int argc, char **argv) {
    if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return 1; }
    if (!glfwVulkanSupported()) { std::cerr << "Vulkan is not supported by this system\n"; glfwTerminate(); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1280, 800, "LuaC Studio — Vulkan", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    App application;
    application.window = window;
    application.compiler = std::getenv("LUAC_COMPILER") ? std::getenv("LUAC_COMPILER") : "./luac";
    const char *shaders = std::getenv("LUAC_SHADER_DIR");
    application.source = "-- LuaC Studio\nlocal function greet(name)\n  return 'Hello, ' .. name\nend\n\nprint(greet('Vulkan'))\n";
    application.cursor = application.source.size();
    if (argc > 1) application.load(argv[1]);
    glfwSetWindowUserPointer(window, &application);
    glfwSetFramebufferSizeCallback(window, resize);
    glfwSetCharCallback(window, characters);
    glfwSetKeyCallback(window, key);
    glfwSetMouseButtonCallback(window, mouseButton);
    glfwSetCursorPosCallback(window, mouseMove);
    try {
        application.renderer.initialize(window, shaders ? shaders : "build");
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            glfwGetFramebufferSize(window, &application.width, &application.height);
            if (application.width > 0 && application.height > 0) application.draw();
            else glfwWaitEvents();
        }
        application.renderer.shutdown();
    } catch (const std::exception &error) {
        std::cerr << "LuaC Studio: " << error.what() << '\n';
        application.renderer.shutdown();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
