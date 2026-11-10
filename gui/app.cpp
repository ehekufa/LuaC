#include "vulkan.hpp"
#include "nuklear_canvas.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {
constexpr size_t sourceCapacity = 1024 * 1024;
constexpr size_t outputCapacity = 512 * 1024;
constexpr size_t nuklearArenaSize = 4 * 1024 * 1024;

struct App {
    GLFWwindow *window = nullptr;
    VulkanRenderer renderer;
    NuklearCanvas canvas;
    nk_context ui{};
    nk_user_font font{};
    std::vector<nk_byte> uiMemory;
    std::vector<char> sourceStorage;
    std::vector<char> observedSource;
    std::vector<char> outputStorage;
    int sourceLength = 0;
    int observedLength = 0;
    int outputLength = 0;
    std::string filename = "untitled.lua";
    std::string compiler;
    int width = 1280, height = 800;
    double mouseX = 0, mouseY = 0;
    bool dirty = false;
    bool controlDown = false;

    void setOutput(const std::string &value) {
        outputLength = static_cast<int>(std::min(value.size(), outputStorage.size() - 1));
        std::memcpy(outputStorage.data(), value.data(), static_cast<size_t>(outputLength));
        outputStorage[static_cast<size_t>(outputLength)] = 0;
    }
    void setSource(const std::string &value) {
        sourceLength = static_cast<int>(std::min(value.size(), sourceStorage.size() - 1));
        std::memcpy(sourceStorage.data(), value.data(), static_cast<size_t>(sourceLength));
        sourceStorage[static_cast<size_t>(sourceLength)] = 0;
        std::memcpy(observedSource.data(), sourceStorage.data(), static_cast<size_t>(sourceLength));
        observedLength = sourceLength;
    }
    static void clipboardCopy(nk_handle handle, const char *text, int length) {
        auto *window = static_cast<GLFWwindow *>(handle.ptr);
        std::string value(text, static_cast<size_t>(length));
        glfwSetClipboardString(window, value.c_str());
    }
    static void clipboardPaste(nk_handle handle, nk_text_edit *edit) {
        auto *window = static_cast<GLFWwindow *>(handle.ptr);
        const char *value = glfwGetClipboardString(window);
        if (value) nk_textedit_paste(edit, value, static_cast<int>(std::strlen(value)));
    }
    void initializeUi() {
        uiMemory.resize(nuklearArenaSize);
        sourceStorage.resize(sourceCapacity);
        observedSource.resize(sourceCapacity);
        outputStorage.resize(outputCapacity);
        std::memset(&font, 0, sizeof(font));
        font.height = 16.0f;
        font.width = [](nk_handle, float height, const char *text, int length) {
            return NuklearCanvas::textWidth(height, text, length);
        };
        if (!nk_init_fixed(&ui, uiMemory.data(), uiMemory.size(), &font))
            throw std::runtime_error("Nuklear fixed arena initialization failed");
        ui.clip.copy = clipboardCopy;
        ui.clip.paste = clipboardPaste;
        ui.clip.userdata = nk_handle_ptr(window);

        nk_color colors[NK_COLOR_COUNT];
        for (auto &entry : colors) entry = nk_rgb(210, 222, 238);
        colors[NK_COLOR_WINDOW] = nk_rgb(16, 22, 34);
        colors[NK_COLOR_HEADER] = nk_rgb(20, 28, 43);
        colors[NK_COLOR_BORDER] = nk_rgb(42, 56, 78);
        colors[NK_COLOR_BUTTON] = nk_rgb(35, 51, 72);
        colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(43, 76, 91);
        colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(42, 157, 132);
        colors[NK_COLOR_TOGGLE] = nk_rgb(42, 56, 78);
        colors[NK_COLOR_TOGGLE_HOVER] = nk_rgb(56, 76, 101);
        colors[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(75, 220, 181);
        colors[NK_COLOR_SELECT] = nk_rgb(35, 51, 72);
        colors[NK_COLOR_SELECT_ACTIVE] = nk_rgb(42, 157, 132);
        colors[NK_COLOR_SLIDER] = nk_rgb(35, 51, 72);
        colors[NK_COLOR_SLIDER_CURSOR] = nk_rgb(75, 220, 181);
        colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(98, 236, 198);
        colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(98, 236, 198);
        colors[NK_COLOR_PROPERTY] = nk_rgb(24, 34, 50);
        colors[NK_COLOR_EDIT] = nk_rgb(11, 17, 28);
        colors[NK_COLOR_COMBO] = nk_rgb(24, 34, 50);
        colors[NK_COLOR_CHART] = nk_rgb(24, 34, 50);
        colors[NK_COLOR_CHART_COLOR] = nk_rgb(75, 220, 181);
        colors[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(98, 236, 198);
        colors[NK_COLOR_SCROLLBAR] = nk_rgb(18, 26, 40);
        colors[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(54, 73, 98);
        colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(72, 96, 126);
        colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(75, 220, 181);
        colors[NK_COLOR_TAB_HEADER] = nk_rgb(20, 28, 43);
        nk_style_from_table(&ui, colors);
        ui.style.window.padding = nk_vec2(12, 10);
        ui.style.window.spacing = nk_vec2(8, 8);
        ui.style.window.rounding = 7.0f;
        ui.style.window.border = 1.0f;
        ui.style.window.fixed_background = nk_style_item_color(nk_rgb(16, 22, 34));
        ui.style.button.rounding = 6.0f;
        ui.style.button.border = 1.0f;
        ui.style.button.text_normal = nk_rgb(218, 231, 242);
        ui.style.button.text_hover = nk_rgb(236, 249, 246);
        ui.style.button.text_active = nk_rgb(8, 25, 25);
        ui.style.edit.rounding = 5.0f;
        ui.style.edit.border = 1.0f;
        ui.style.edit.cursor_normal = nk_rgb(75, 220, 181);
        ui.style.edit.cursor_hover = nk_rgb(98, 236, 198);
        ui.style.edit.cursor_text_normal = nk_rgb(11, 17, 28);
        ui.style.edit.cursor_text_hover = nk_rgb(11, 17, 28);
        ui.style.edit.text_normal = nk_rgb(210, 222, 238);
        ui.style.edit.text_hover = nk_rgb(225, 235, 246);
        ui.style.edit.text_active = nk_rgb(225, 235, 246);
        ui.style.window.scrollbar_size = nk_vec2(10, 10);
        setOutput("Ready. F5 - run, F6 - syntax check, Ctrl+S - save.\nNuklear immediate UI / fixed 4 MiB arena / Vulkan renderer.");
    }
    void load(const char *path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) { setOutput(std::string("Cannot open: ") + path); return; }
        std::ostringstream bytes; bytes << file.rdbuf();
        std::string value = bytes.str();
        if (value.size() >= sourceStorage.size()) {
            setOutput("File is larger than the 1 MiB editor buffer.");
            return;
        }
        setSource(value);
        filename = path; dirty = false;
        setOutput(std::string("Opened ") + path);
    }
    void save() {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file) { setOutput("Save failed: " + filename); return; }
        file.write(sourceStorage.data(), sourceLength);
        dirty = false; setOutput("Saved " + filename);
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
        if (!pipe) { setOutput(std::string("Cannot start compiler: ") + std::strerror(errno)); return; }
        char buffer[1024]; std::string log;
        while (fgets(buffer, sizeof(buffer), pipe)) log += buffer;
        int status = pclose(pipe);
        setOutput(log.empty() ? (status == 0 ? "Done." : "Compiler failed.") : log);
    }
    void run() {
        if (dirty) save();
        execute("--jit " + quote(filename));
    }
    void check() {
        std::string temp = filename + ".check.lua";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file.write(sourceStorage.data(), sourceLength);
        }
        execute("--check " + quote(temp));
        std::remove(temp.c_str());
    }
    void toolbar() {
        if (nk_begin(&ui, "Toolbar", nk_rect(0, 0, static_cast<float>(width), 64),
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
            nk_layout_row_begin(&ui, NK_STATIC, 38, 5);
            nk_layout_row_push(&ui, 140);
            nk_label_colored(&ui, "LuaC // STUDIO", NK_TEXT_LEFT, nk_rgb(75, 220, 181));
            nk_layout_row_push(&ui, 86);
            if (nk_button_label(&ui, "Run  F5")) run();
            nk_layout_row_push(&ui, 92);
            if (nk_button_label(&ui, "Check F6")) check();
            nk_layout_row_push(&ui, 76);
            if (nk_button_label(&ui, "Save")) save();
            nk_layout_row_push(&ui, std::max(40.0f, width - 450.0f));
            nk_label(&ui, filename.c_str(), NK_TEXT_RIGHT);
            nk_layout_row_end(&ui);
        }
        nk_end(&ui);
    }
    void editor(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Source", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 22, 1);
            nk_label_colored(&ui, dirty ? "SOURCE  /  modified" : "SOURCE", NK_TEXT_LEFT,
                             nk_rgb(75, 220, 181));
            nk_layout_row_dynamic(&ui, std::max(30.0f, h - 66), 1);
            nk_edit_string(&ui, NK_EDIT_BOX, sourceStorage.data(), &sourceLength,
                           static_cast<int>(sourceStorage.size()), nk_filter_default);
            if (sourceLength != observedLength ||
                std::memcmp(sourceStorage.data(), observedSource.data(), static_cast<size_t>(sourceLength)) != 0) {
                std::memcpy(observedSource.data(), sourceStorage.data(), static_cast<size_t>(sourceLength));
                observedLength = sourceLength;
                dirty = true;
            }
        }
        nk_end(&ui);
    }
    void output(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Output", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 22, 1);
            nk_label_colored(&ui, "OUTPUT  /  native compiler", NK_TEXT_LEFT, nk_rgb(75, 220, 181));
            nk_layout_row_dynamic(&ui, std::max(30.0f, h - 66), 1);
            nk_edit_string(&ui, NK_EDIT_BOX | NK_EDIT_READ_ONLY, outputStorage.data(), &outputLength,
                           static_cast<int>(outputStorage.size()), nk_filter_default);
        }
        nk_end(&ui);
    }
    void footer() {
        if (nk_begin(&ui, "Status", nk_rect(0, static_cast<float>(height - 28),
                     static_cast<float>(width), 28), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 18, 1);
            char status[160];
            std::snprintf(status, sizeof(status), "%d bytes  |  Nuklear C99  |  Vulkan  |  F5 run  F6 check  Ctrl+S save", sourceLength);
            nk_label(&ui, status, NK_TEXT_LEFT);
        }
        nk_end(&ui);
    }
    void draw() {
        toolbar();
        float bodyHeight = static_cast<float>(std::max(40, height - 96));
        float split = std::floor(width * 0.62f);
        editor(8, 68, split - 12, bodyHeight);
        output(split + 4, 68, width - split - 12, bodyHeight);
        footer();
        canvas.render(ui, width, height);
        renderer.render(canvas.vertices, canvas.draws, width, height);
        nk_clear(&ui);
    }
    void pointer(double x, double y) {
        int logicalWidth = 1, logicalHeight = 1;
        glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
        mouseX = x * width / std::max(logicalWidth, 1);
        mouseY = y * height / std::max(logicalHeight, 1);
        nk_input_motion(&ui, static_cast<int>(mouseX), static_cast<int>(mouseY));
    }
};

App *app(GLFWwindow *window) { return static_cast<App *>(glfwGetWindowUserPointer(window)); }
void resize(GLFWwindow *window, int width, int height) {
    App *a = app(window); a->width = std::max(width, 1); a->height = std::max(height, 1);
}
void characters(GLFWwindow *window, unsigned int codepoint) {
    if (codepoint >= 32 && codepoint != 127) nk_input_unicode(&app(window)->ui, codepoint);
}
void key(GLFWwindow *window, int keyCode, int, int action, int mods) {
    App *a = app(window);
    bool down = action != GLFW_RELEASE;
    if (keyCode == GLFW_KEY_LEFT_CONTROL || keyCode == GLFW_KEY_RIGHT_CONTROL) {
        a->controlDown = down; nk_input_key(&a->ui, NK_KEY_CTRL, down); return;
    }
    if (keyCode == GLFW_KEY_LEFT_SHIFT || keyCode == GLFW_KEY_RIGHT_SHIFT) {
        nk_input_key(&a->ui, NK_KEY_SHIFT, down); return;
    }
    if (keyCode == GLFW_KEY_LEFT_ALT || keyCode == GLFW_KEY_RIGHT_ALT) {
        nk_input_key(&a->ui, NK_KEY_ALT, down); return;
    }
    if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && keyCode == GLFW_KEY_S) { a->save(); return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_F5) { a->run(); return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_F6) { a->check(); return; }
    nk_keys mapped = NK_KEY_NONE;
    switch (keyCode) {
    case GLFW_KEY_BACKSPACE: mapped = NK_KEY_BACKSPACE; break;
    case GLFW_KEY_DELETE: mapped = NK_KEY_DEL; break;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: mapped = NK_KEY_ENTER; break;
    case GLFW_KEY_TAB: mapped = NK_KEY_TAB; break;
    case GLFW_KEY_LEFT: mapped = a->controlDown ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT; break;
    case GLFW_KEY_RIGHT: mapped = a->controlDown ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT; break;
    case GLFW_KEY_UP: mapped = NK_KEY_UP; break;
    case GLFW_KEY_DOWN: mapped = NK_KEY_DOWN; break;
    case GLFW_KEY_HOME: mapped = a->controlDown ? NK_KEY_TEXT_START : NK_KEY_TEXT_LINE_START; break;
    case GLFW_KEY_END: mapped = a->controlDown ? NK_KEY_TEXT_END : NK_KEY_TEXT_LINE_END; break;
    case GLFW_KEY_PAGE_UP: mapped = NK_KEY_SCROLL_UP; break;
    case GLFW_KEY_PAGE_DOWN: mapped = NK_KEY_SCROLL_DOWN; break;
    case GLFW_KEY_INSERT: mapped = NK_KEY_TEXT_INSERT_MODE; break;
    case GLFW_KEY_A: if (a->controlDown) mapped = NK_KEY_TEXT_SELECT_ALL; break;
    case GLFW_KEY_C: if (a->controlDown) mapped = NK_KEY_COPY; break;
    case GLFW_KEY_X: if (a->controlDown) mapped = NK_KEY_CUT; break;
    case GLFW_KEY_V: if (a->controlDown) mapped = NK_KEY_PASTE; break;
    case GLFW_KEY_Z: if (a->controlDown) mapped = NK_KEY_TEXT_UNDO; break;
    case GLFW_KEY_Y: if (a->controlDown) mapped = NK_KEY_TEXT_REDO; break;
    default: break;
    }
    if (mapped != NK_KEY_NONE) nk_input_key(&a->ui, mapped, down);
}
void mouseButton(GLFWwindow *window, int button, int action, int) {
    nk_buttons mapped;
    if (button == GLFW_MOUSE_BUTTON_LEFT) mapped = NK_BUTTON_LEFT;
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) mapped = NK_BUTTON_MIDDLE;
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) mapped = NK_BUTTON_RIGHT;
    else return;
    App *a = app(window);
    nk_input_button(&a->ui, mapped, static_cast<int>(a->mouseX), static_cast<int>(a->mouseY),
                    action != GLFW_RELEASE);
}
void mouseMove(GLFWwindow *window, double x, double y) { app(window)->pointer(x, y); }
void scroll(GLFWwindow *window, double x, double y) {
    nk_input_scroll(&app(window)->ui, nk_vec2(static_cast<float>(x), static_cast<float>(y)));
}
}

int main(int argc, char **argv) {
    if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return 1; }
    if (!glfwVulkanSupported()) { std::cerr << "Vulkan is not supported by this system\n"; glfwTerminate(); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1280, 800, "LuaC Studio - Nuklear + Vulkan", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    App application;
    application.window = window;
    application.compiler = std::getenv("LUAC_COMPILER") ? std::getenv("LUAC_COMPILER") : "./luac";
    const char *shaders = std::getenv("LUAC_SHADER_DIR");
    glfwSetWindowUserPointer(window, &application);
    glfwSetFramebufferSizeCallback(window, resize);
    glfwSetCharCallback(window, characters);
    glfwSetKeyCallback(window, key);
    glfwSetMouseButtonCallback(window, mouseButton);
    glfwSetCursorPosCallback(window, mouseMove);
    glfwSetScrollCallback(window, scroll);
    try {
        application.initializeUi();
        application.setSource("-- LuaC Studio / Nuklear immediate UI\nlocal function greet(name)\n  return 'Hello, ' .. name\nend\n\nprint(greet('Vulkan'))\n");
        if (argc > 1) application.load(argv[1]);
        application.renderer.initialize(window, shaders ? shaders : "build");
        while (!glfwWindowShouldClose(window)) {
            nk_input_begin(&application.ui);
            glfwPollEvents();
            nk_input_end(&application.ui);
            glfwGetFramebufferSize(window, &application.width, &application.height);
            if (application.width > 0 && application.height > 0) application.draw();
            else { nk_clear(&application.ui); glfwWaitEvents(); }
        }
        nk_free(&application.ui);
        application.renderer.shutdown();
    } catch (const std::exception &error) {
        std::cerr << "LuaC Studio: " << error.what() << '\n';
        nk_free(&application.ui);
        application.renderer.shutdown();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
