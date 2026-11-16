#include "vulkan.hpp"
#include "nuklear_canvas.hpp"
#include "color_wheel.hpp"
#include "color_hex.hpp"
#include "engine.hpp"
#include "project_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>

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
    nk_user_font *font = nullptr;
    std::deque<nk_user_font> overlayFonts;
    bool russian = false;
    bool settingsOpen = false;
    enum class Page { Projects, Files, Editor } page = Page::Projects;
    enum class Dialog { None, NewProject, NewFile, NewFolder, Rename, Delete } dialog = Dialog::None;
    std::unique_ptr<ProjectStore> projects;
    ProjectStore::Path project, directory, dialogTarget;
    std::vector<ProjectStore::Path> entries;
    char dialogName[256]{};
    int dialogNameLength = 0;
    std::string projectError;
    ColorWheel themeWheel;
    bool themeDirty = false;
    char themeHex[8]{};
    int themeHexLength = 7;
    bool themeHexInvalid = false;
    const char *tr(const char *en, const char *ru) const { return russian ? ru : en; }
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
    nk_keys pressedKeys[GLFW_KEY_LAST + 1]{};
    bool engineMode = false;
    luac::engine::World world;
    std::unordered_map<std::string, uint32_t> textureIds;
    std::unordered_set<std::string> textureFailures;
    luac::engine::World initialWorld;
    luac::engine::EntityId selectedEntity = luac::engine::invalidEntity;

    void initializeEngine() {
        auto &player = world.create("Player", {-140, 0});
        player.body.velocity = {55, 20};
        player.body.solid = true;
        player.sprite.size = {44, 44};
        player.sprite.r = 0.20; player.sprite.g = 0.85; player.sprite.b = 0.65;
        player.tags.insert("player");
        player.components["health"] = 100;
        auto &crate = world.create("Crate", {30, 25});
        crate.body.kinematic = true;
        crate.body.solid = true;
        crate.sprite.size = {64, 64};
        crate.sprite.r = 0.85; crate.sprite.g = 0.55; crate.sprite.b = 0.25;
        crate.tags.insert("obstacle");
        auto &orb = world.create("Orb", {150, -75});
        orb.body.velocity = {-25, 30};
        orb.body.solid = true;
        orb.body.angularVelocity = 1.5;
        orb.sprite.size = {30, 30};
        orb.sprite.r = 0.55; orb.sprite.g = 0.40; orb.sprite.b = 1.0;
        world.pause();
        selectedEntity = player.id;
        initialWorld = world;
    }
    void resetEngine() {
        world = initialWorld;
        selectedEntity = world.entities().empty() ? luac::engine::invalidEntity : world.entities().front().id;
        setOutput(tr("Scene reset. Fixed step: 60 Hz.", "Сцена сброшена. Шаг: 60 Гц."));
    }
    luac::engine::Entity *selected() { return world.find(selectedEntity); }

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
    std::string settingsPath() const {
#ifdef _WIN32
        const char *home = std::getenv("APPDATA");
#else
        const char *home = std::getenv("HOME");
#endif
        return home && *home ? std::string(home) + "/.luac-studio.conf" : std::string();
    }
    void loadTheme() {
        std::ifstream file(settingsPath());
        unsigned r, g, b;
        if (file >> r >> g >> b && r <= 255 && g <= 255 && b <= 255)
            themeWheel.setColor(nk_rgb(r, g, b));
    }
    void saveTheme() {
        if (!themeDirty) return;
        std::string path = settingsPath();
        if (path.empty()) return;
        std::ofstream file(path, std::ios::trunc);
        nk_color c = themeWheel.color();
        if (file << static_cast<unsigned>(c.r) << ' ' << static_cast<unsigned>(c.g) << ' ' << static_cast<unsigned>(c.b) << '\n')
            themeDirty = false;
    }
    void applyTheme() {
        nk_color accent = themeWheel.color();
        float h, s, v;
        nk_color_hsv_f(&h, &s, &v, accent);
        nk_color dark = nk_hsv_f(h, s, std::min(v, 0.36f));
        nk_color hover = nk_hsv_f(h, s, std::min(v, 0.26f));
        ui.style.button.hover = nk_style_item_color(hover);
        ui.style.button.active = nk_style_item_color(dark);
        ui.style.option.cursor_normal = ui.style.option.cursor_hover = nk_style_item_color(accent);
        ui.style.checkbox.cursor_normal = ui.style.checkbox.cursor_hover = nk_style_item_color(accent);
        ui.style.selectable.normal_active = ui.style.selectable.hover_active = ui.style.selectable.pressed_active = nk_style_item_color(dark);
        ui.style.slider.cursor_normal = ui.style.slider.cursor_hover = ui.style.slider.cursor_active = nk_style_item_color(accent);
        ui.style.slider.bar_filled = accent;
        ui.style.edit.cursor_normal = ui.style.edit.cursor_hover = dark;
        ui.style.scrollv.cursor_active = nk_style_item_color(accent);
        ui.style.scrollh.cursor_active = nk_style_item_color(accent);
    }
    void initializeUi() {
        uiMemory.resize(nuklearArenaSize);
        sourceStorage.resize(sourceCapacity);
        observedSource.resize(sourceCapacity);
        outputStorage.resize(outputCapacity);
        font = canvas.atlas().font();
        if (!nk_init_fixed(&ui, uiMemory.data(), uiMemory.size(), font))
            throw std::runtime_error("Nuklear fixed arena initialization failed");
        ui.clip.copy = clipboardCopy;
        ui.clip.paste = clipboardPaste;
        ui.clip.userdata = nk_handle_ptr(window);

        nk_color colors[NK_COLOR_COUNT];
        for (auto &entry : colors) entry = nk_rgb(255, 255, 255);
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
        ui.style.button.text_normal = nk_rgb(255, 255, 255);
        ui.style.button.text_hover = nk_rgb(255, 255, 255);
        ui.style.button.text_active = nk_rgb(255, 255, 255);
        ui.style.edit.rounding = 5.0f;
        ui.style.edit.border = 1.0f;
        ui.style.edit.cursor_normal = nk_rgb(75, 220, 181);
        ui.style.edit.cursor_hover = nk_rgb(98, 236, 198);
        ui.style.edit.cursor_text_normal = nk_rgb(255, 255, 255);
        ui.style.edit.cursor_text_hover = nk_rgb(255, 255, 255);
        ui.style.edit.text_normal = nk_rgb(255, 255, 255);
        ui.style.edit.text_hover = nk_rgb(255, 255, 255);
        ui.style.edit.text_active = nk_rgb(255, 255, 255);
        ui.style.window.scrollbar_size = nk_vec2(10, 10);
        loadTheme();
        formatColorHex(themeWheel.color(), themeHex);
        applyTheme();
        setOutput("Ready. F5 - run, F6 - syntax check, F7 - Editor/Engine, Ctrl+S - save.\nNuklear immediate UI / fixed 4 MiB arena / Vulkan renderer.");
        initializeEngine();
    }
    void load(const char *path) {
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        if (!file) { setOutput(std::string(tr("Cannot open: ", "Не удалось открыть: ")) + path); return; }
        std::ostringstream bytes; bytes << file.rdbuf();
        std::string value = bytes.str();
        if (value.size() >= sourceStorage.size()) {
            setOutput(tr("File exceeds the 1 MiB editor buffer.", "Файл больше буфера редактора (1 МиБ)."));
            return;
        }
        setSource(value);
        filename = path; dirty = false;
        setOutput(std::string(tr("Opened ", "Открыт: ")) + path);
    }
    bool save() {
        std::ofstream file(std::filesystem::u8path(filename), std::ios::binary | std::ios::trunc);
        if (!file || !file.write(sourceStorage.data(), sourceLength) || !file.flush()) {
            setOutput(std::string(tr("Save failed: ", "Не удалось сохранить: ")) + filename); return false;
        }
        dirty = false; setOutput(std::string(tr("Saved ", "Сохранён: ")) + filename);
        return true;
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
    void execute(const std::string &arguments, const ProjectStore::Path &workingDirectory = {}) {
        std::string command;
        if (!workingDirectory.empty()) {
#ifdef _WIN32
            command = "cd /d " + quote(workingDirectory.u8string()) + " && ";
#else
            command = "cd " + quote(workingDirectory.u8string()) + " && ";
#endif
        }
#ifdef _WIN32
        // cmd.exe expands percent variables even within quotes. Refuse paths
        // that would be interpreted by the shell instead of executing them.
        if (compiler.find_first_of("%!") != std::string::npos ||
            workingDirectory.u8string().find_first_of("%!") != std::string::npos ||
            arguments.find_first_of("%!") != std::string::npos) {
            setOutput(tr("Cannot execute paths containing % or !", "Нельзя запустить путь с символами % или !")); return;
        }
#endif
        command += quote(compiler) + " " + arguments + " 2>&1";
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe) { setOutput(std::string(tr("Cannot start compiler: ", "Не удалось запустить компилятор: ")) + std::strerror(errno)); return; }
        char buffer[1024]; std::string log;
        while (fgets(buffer, sizeof(buffer), pipe)) log += buffer;
        int status = pclose(pipe);
        setOutput(log.empty() ? (status == 0 ? tr("Done.", "Готово.") : tr("Compiler failed.", "Ошибка компилятора.")) : log);
    }
    void run() {
        if (page == Page::Editor && dirty && !save()) return;
        if (!project.empty()) {
            if (!std::filesystem::is_regular_file(project / "main.lua")) {
                setOutput(tr("main.lua is missing", "Файл main.lua не найден")); return;
            }
            execute("--jit " + quote("main.lua"), project);
        } else execute("--jit " + quote(filename));
    }
    void check() {
        namespace fs = std::filesystem;
        fs::path temporary;
        try {
            auto base = fs::temp_directory_path();
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int i = 0; i < 100; ++i) {
                auto candidate = base / ("luac-check-" + std::to_string(stamp) + "-" + std::to_string(i));
                if (fs::create_directory(candidate)) { temporary = candidate; break; }
            }
            if (temporary.empty()) throw std::runtime_error("Cannot create temporary directory");
            auto path = temporary / "source.lua";
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(sourceStorage.data(), sourceLength) || !file.flush())
                throw std::runtime_error("Cannot write temporary source");
            file.close();
            execute("--check " + quote(path.u8string()));
            fs::remove_all(temporary);
        } catch (const std::exception &error) {
            if (!temporary.empty()) { std::error_code ignored; fs::remove_all(temporary, ignored); }
            setOutput(error.what());
        }
    }
    void initializeProjects() {
        try {
#ifdef _WIN32
            const char *home = std::getenv("APPDATA");
#else
            const char *home = std::getenv("HOME");
#endif
            auto root = home && *home ? std::filesystem::u8path(home) / "LuaC" / "projects" :
                std::filesystem::current_path() / "LuaC-projects";
            projects = std::make_unique<ProjectStore>(root);
            entries = projects->projects();
        } catch (const std::exception &error) { projectError = error.what(); }
    }
    void refreshProjects() {
        try { entries = projects->projects(); projectError.clear(); }
        catch (const std::exception &error) { projectError = error.what(); }
    }
    void refreshFiles() {
        try { entries = projects->files(directory); projectError.clear(); }
        catch (const std::exception &error) { projectError = error.what(); }
    }
    void showProjects() {
        if (page == Page::Editor && dirty && !save()) return;
        engineMode = false; page = Page::Projects; project.clear(); directory.clear();
        dialog = Dialog::None;
        if (projects) refreshProjects();
    }
    void showFiles(const ProjectStore::Path &path) {
        directory = path; engineMode = false; page = Page::Files; refreshFiles();
        if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "page=files\n");
    }
    void openProject(const ProjectStore::Path &path) {
        project = path; showFiles(path);
    }
    void openProjectFile(const ProjectStore::Path &path) {
        std::string extension = path.extension().u8string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".lua" && extension != ".txt") {
            projectError = tr("Only Lua and text files can be opened here for now.", "Пока здесь можно открыть только Lua и текстовые файлы.");
            return;
        }
        load(path.u8string().c_str());
        if (filename == path.u8string()) { page = Page::Editor; projectError.clear(); if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "page=editor\n"); }
    }
    void openDialog(Dialog kind, const ProjectStore::Path &target = {}) {
        dialog = kind; dialogTarget = target; projectError.clear();
        if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "dialog=%d\n", static_cast<int>(kind));
        std::string name = kind == Dialog::Rename ? target.filename().u8string() : "";
        dialogNameLength = static_cast<int>(std::min(name.size(), sizeof(dialogName) - 1));
        std::memcpy(dialogName, name.data(), static_cast<size_t>(dialogNameLength));
        dialogName[dialogNameLength] = 0;
    }
    void projectList(float h) {
        float panelWidth = std::min(960.0f, width - 32.0f);
        float x = (width - panelWidth) * .5f;
        if (nk_begin(&ui, "Projects list", nk_rect(x, 68, panelWidth, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 34, 2);
            nk_label_colored(&ui, tr("Projects", "Проекты"), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            if (projects && nk_button_label(&ui, tr("+ New project", "+ Новый проект"))) openDialog(Dialog::NewProject);
            if (!projectError.empty()) { nk_layout_row_dynamic(&ui, 30, 1); nk_label(&ui, projectError.c_str(), NK_TEXT_LEFT); }
            if (entries.empty()) { nk_layout_row_dynamic(&ui, 32, 1); nk_label(&ui, tr("Create a project to get started.", "Создайте проект, чтобы начать."), NK_TEXT_LEFT); }
            for (const auto &item : std::vector<ProjectStore::Path>(entries)) {
                nk_layout_row_begin(&ui, NK_DYNAMIC, 48, 4);
                nk_layout_row_push(&ui, .55f);
                if (nk_button_label(&ui, item.filename().u8string().c_str())) openProject(item);
                nk_layout_row_push(&ui, .15f);
                if (nk_button_label(&ui, tr("Run", "Запуск"))) { openProject(item); run(); }
                nk_layout_row_push(&ui, .15f);
                if (nk_button_label(&ui, tr("Rename", "Имя"))) openDialog(Dialog::Rename, item);
                nk_layout_row_push(&ui, .15f);
                if (nk_button_label(&ui, tr("Delete", "Удалить"))) openDialog(Dialog::Delete, item);
                nk_layout_row_end(&ui);
                if (page != Page::Projects) break;
            }
        }
        nk_end(&ui);
    }
    void fileList(float h) {
        float consoleHeight = std::min(150.0f, std::max(100.0f, h * .22f));
        float panelWidth = std::min(960.0f, width - 32.0f);
        float x = (width - panelWidth) * .5f;
        if (nk_begin(&ui, "Project files", nk_rect(x, 68, panelWidth, h - consoleHeight - 8), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 34, 4);
            if (nk_button_label(&ui, tr("Back", "Назад"))) {
                if (directory == project) showProjects(); else showFiles(directory.parent_path());
            }
            if (nk_button_label(&ui, tr("+ File", "+ Файл"))) openDialog(Dialog::NewFile);
            if (nk_button_label(&ui, tr("+ Folder", "+ Папка"))) openDialog(Dialog::NewFolder);
            if (nk_button_label(&ui, tr("Run project", "Запустить"))) run();
            nk_layout_row_dynamic(&ui, 28, 1);
            std::string title = project.filename().u8string() + " / " + directory.lexically_relative(project).u8string();
            nk_label_colored(&ui, title.c_str(), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            if (!projectError.empty()) { nk_layout_row_dynamic(&ui, 28, 1); nk_label(&ui, projectError.c_str(), NK_TEXT_LEFT); }
            for (const auto &item : std::vector<ProjectStore::Path>(entries)) {
                std::error_code error;
                bool folder = std::filesystem::is_directory(item, error);
                std::string name = (folder ? "[+]  " : "      ") + item.filename().u8string();
                nk_layout_row_begin(&ui, NK_DYNAMIC, 42, 3);
                nk_layout_row_push(&ui, .68f);
                if (nk_button_label(&ui, name.c_str())) { if (folder) showFiles(item); else openProjectFile(item); }
                nk_layout_row_push(&ui, .16f);
                if (nk_button_label(&ui, tr("Rename", "Имя"))) openDialog(Dialog::Rename, item);
                nk_layout_row_push(&ui, .16f);
                if (nk_button_label(&ui, tr("Delete", "Удалить"))) openDialog(Dialog::Delete, item);
                nk_layout_row_end(&ui);
                if (page != Page::Files || directory != item.parent_path()) break;
            }
        }
        nk_end(&ui);
        output(x, 68 + h - consoleHeight, panelWidth, consoleHeight);
    }
    void projectDialog() {
        if (dialog == Dialog::None) return;
        float w = std::min(460.0f, width - 32.0f), h = 220.0f;
        if (nk_begin(&ui, "Project dialog", nk_rect((width - w) / 2, (height - h) / 2, w, h), NK_WINDOW_BORDER)) {
            const char *heading = dialog == Dialog::NewProject ? tr("New project", "Новый проект") :
                dialog == Dialog::NewFile ? tr("New file", "Новый файл") :
                dialog == Dialog::NewFolder ? tr("New folder", "Новая папка") :
                dialog == Dialog::Rename ? tr("Rename", "Переименовать") : tr("Delete?", "Удалить?");
            nk_layout_row_dynamic(&ui, 30, 1); nk_label_colored(&ui, heading, NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            nk_layout_row_dynamic(&ui, 34, 1);
            if (dialog == Dialog::Delete) nk_label(&ui, dialogTarget.filename().u8string().c_str(), NK_TEXT_LEFT);
            else { nk_edit_string(&ui, NK_EDIT_FIELD, dialogName, &dialogNameLength, sizeof(dialogName), nk_filter_default); dialogName[dialogNameLength] = 0; }
            nk_layout_row_dynamic(&ui, 28, 1);
            nk_label(&ui, projectError.c_str(), NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ui, 34, 2);
            if (nk_button_label(&ui, tr("Cancel", "Отмена"))) dialog = Dialog::None;
            if (nk_button_label(&ui, dialog == Dialog::Delete ? tr("Delete", "Удалить") : tr("Save", "Сохранить"))) {
                try {
                    Dialog action = dialog;
                    std::string name(dialogName, static_cast<size_t>(dialogNameLength));
                    if (action == Dialog::NewProject) { auto created = projects->createProject(name); dialog = Dialog::None; openProject(created); }
                    else if (action == Dialog::NewFile) { auto created = projects->createFile(directory, name); dialog = Dialog::None; openProjectFile(created); }
                    else if (action == Dialog::NewFolder) { projects->createFolder(directory, name); dialog = Dialog::None; refreshFiles(); }
                    else if (action == Dialog::Rename) { projects->rename(dialogTarget, name); dialog = Dialog::None; if (page == Page::Projects) refreshProjects(); else refreshFiles(); }
                    else if (action == Dialog::Delete) { projects->remove(dialogTarget); dialog = Dialog::None; if (page == Page::Projects) refreshProjects(); else refreshFiles(); }
                } catch (const std::exception &error) { projectError = error.what(); }
            }
        }
        nk_end(&ui);
    }
    void toolbar() {
        if (nk_begin(&ui, "Toolbar", nk_rect(0, 0, static_cast<float>(width), 64),
                     NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
            nk_layout_row_begin(&ui, NK_STATIC, 38, 10);
            nk_layout_row_push(&ui, 112);
            nk_label_colored(&ui, "LuaC // STUDIO", NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            nk_layout_row_push(&ui, 100);
            if (nk_button_label(&ui, tr("Settings", "Настройки"))) {
                settingsOpen = !settingsOpen;
                if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "settings=%s\n", settingsOpen ? "open" : "closed");
            }
            nk_layout_row_push(&ui, 100);
            if (nk_button_label(&ui, tr("Projects", "Проекты"))) showProjects();
            nk_layout_row_push(&ui, 90);
            if (nk_button_label(&ui, tr("Engine", "Движок"))) engineMode = true;
            nk_layout_row_push(&ui, 100);
            if (!engineMode) { if (nk_button_label(&ui, tr("Run F5", "Пуск F5"))) { if (page != Page::Projects) run(); } }
            else if (nk_button_label(&ui, world.running() ? tr("Pause", "Пауза") : tr("Play", "Играть"))) {
                if (world.running()) world.pause(); else world.play();
            }
            nk_layout_row_push(&ui, 100);
            if (engineMode) {
                if (nk_button_label(&ui, tr("Step", "Шаг"))) world.step();
            } else if (nk_button_label(&ui, tr("Check", "Проверка")) && page == Page::Editor) check();
            nk_layout_row_push(&ui, 100);
            if (nk_button_label(&ui, engineMode ? tr("Add", "Добавить") : tr("Save", "Сохранить"))) {
                if (engineMode) {
                    auto &entity = world.create("Entity " + std::to_string(world.count() + 1));
                    selectedEntity = entity.id;
                } else if (page == Page::Editor) save();
            }
            nk_layout_row_push(&ui, 100);
            if (nk_button_label(&ui, engineMode ? tr("Reset", "Сброс") : "F6")) { if (engineMode) resetEngine(); else if (page == Page::Editor) check(); }
            nk_layout_row_push(&ui, 100);
            if (nk_button_label(&ui, tr("Save", "Сохранить")) && page == Page::Editor) save();
            nk_layout_row_push(&ui, std::max(40.0f, width - 920.0f));
            nk_label(&ui, filename.c_str(), NK_TEXT_RIGHT);
            nk_layout_row_end(&ui);
        }
        nk_end(&ui);
    }
    void editor(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Source", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 26, 2);
            nk_label_colored(&ui, dirty ? tr("Script • modified", "Скрипт • изменён") : filename.c_str(), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            if (!project.empty() && nk_button_label(&ui, tr("Back to project files", "К файлам проекта"))) {
                if (!dirty || save()) showFiles(directory);
            }
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
            nk_label_colored(&ui, tr("OUTPUT  /  native compiler", "ВЫВОД  /  компилятор"), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            nk_layout_row_dynamic(&ui, std::max(30.0f, h - 66), 1);
            nk_edit_string(&ui, NK_EDIT_BOX | NK_EDIT_READ_ONLY, outputStorage.data(), &outputLength,
                           static_cast<int>(outputStorage.size()), nk_filter_default);
        }
        nk_end(&ui);
    }
    void hierarchy(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Scene", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 22, 1);
            nk_label_colored(&ui, tr("SCENE  /  entities", "СЦЕНА  /  объекты"), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            for (auto &entity : world.entities()) {
                nk_layout_row_dynamic(&ui, 28, 1);
                nk_bool active = selectedEntity == entity.id;
                std::string label = std::to_string(entity.id) + "  " + entity.name;
                if (nk_selectable_label(&ui, label.c_str(), NK_TEXT_LEFT, &active)) selectedEntity = entity.id;
            }
            nk_layout_row_dynamic(&ui, 30, 2);
            if (nk_button_label(&ui, tr("Add", "Добавить"))) {
                auto &entity = world.create("Entity " + std::to_string(world.count() + 1));
                selectedEntity = entity.id;
            }
            if (nk_button_label(&ui, tr("Duplicate", "Копировать")) && selected())
                selectedEntity = world.duplicate(selectedEntity).id;
        }
        nk_end(&ui);
    }
    void viewport(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Viewport", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(&ui, 20, 1);
            char caption[160];
            std::snprintf(caption, sizeof(caption), tr("VIEWPORT  |  %zu entities  |  %.3fs  |  60 Hz fixed step", "СЦЕНА  |  объектов: %zu  |  %.3fс  |  шаг 60 Гц"),
                          world.count(), world.time());
            nk_label_colored(&ui, caption, NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            nk_layout_space_begin(&ui, NK_STATIC, std::max(30.0f, h - 56), 1);
            struct nk_rect bounds = nk_layout_space_bounds(&ui);
            nk_command_buffer *commands = nk_window_get_canvas(&ui);
            nk_fill_rect(commands, bounds, 0, nk_rgb(9, 15, 25));
            const float centerX = bounds.x + bounds.w * 0.5f;
            const float centerY = bounds.y + bounds.h * 0.5f;
            const float zoom = static_cast<float>(world.camera.zoom);
            for (int gx = 0; gx < static_cast<int>(bounds.w); gx += 40)
                nk_stroke_line(commands, bounds.x + gx, bounds.y, bounds.x + gx, bounds.y + bounds.h, 1, nk_rgb(25, 38, 54));
            for (int gy = 0; gy < static_cast<int>(bounds.h); gy += 40)
                nk_stroke_line(commands, bounds.x, bounds.y + gy, bounds.x + bounds.w, bounds.y + gy, 1, nk_rgb(25, 38, 54));
            nk_stroke_line(commands, centerX, bounds.y, centerX, bounds.y + bounds.h, 1, nk_rgb(45, 71, 83));
            nk_stroke_line(commands, bounds.x, centerY, bounds.x + bounds.w, centerY, 1, nk_rgb(45, 71, 83));
            for (const auto &entity : world.entities()) {
                if (!entity.active) continue;
                auto screen = world.camera.worldToScreen(entity.transform.position);
                float ex = centerX + static_cast<float>(screen.x);
                float ey = centerY + static_cast<float>(screen.y);
                float ew = static_cast<float>(entity.sprite.size.x * entity.transform.scale) * zoom;
                float eh = static_cast<float>(entity.sprite.size.y * entity.transform.scale) * zoom;
                struct nk_rect rectangle = nk_rect(ex - ew / 2, ey - eh / 2, ew, eh);
                nk_color color = nk_rgba(static_cast<int>(entity.sprite.r * 255), static_cast<int>(entity.sprite.g * 255),
                                         static_cast<int>(entity.sprite.b * 255), static_cast<int>(entity.sprite.a * 255));
                if (!entity.sprite.texture.empty()) {
                    try {
                        auto found = textureIds.find(entity.sprite.texture);
                        if (found == textureIds.end() && !textureFailures.count(entity.sprite.texture))
                            found = textureIds.emplace(entity.sprite.texture, renderer.loadTexture(entity.sprite.texture)).first;
                        if (found == textureIds.end()) {
                            nk_fill_rect(commands, rectangle, 4, color);
                        } else {
                            auto coordinate = [](double value) {
                                return static_cast<nk_ushort>(std::max(0.0, std::min(1.0, value)) * 65535.0);
                            };
                            struct nk_image image{};
                            image.handle = nk_handle_id(static_cast<int>(found->second));
                            image.w = image.h = 65535;
                            image.region[0] = coordinate(entity.sprite.u0);
                            image.region[1] = coordinate(entity.sprite.v0);
                            nk_ushort u1 = coordinate(entity.sprite.u1), v1 = coordinate(entity.sprite.v1);
                            image.region[2] = u1 >= image.region[0] ? static_cast<nk_ushort>(u1 - image.region[0]) : 0;
                            image.region[3] = v1 >= image.region[1] ? static_cast<nk_ushort>(v1 - image.region[1]) : 0;
                            nk_draw_image(commands, rectangle, &image, color);
                        }
                    } catch (const std::exception &) {
                        textureFailures.insert(entity.sprite.texture);
                        nk_fill_rect(commands, rectangle, 4, color);
                    }
                } else nk_fill_rect(commands, rectangle, 4, color);
                if (entity.id == selectedEntity) nk_stroke_rect(commands, nk_rect(rectangle.x - 3, rectangle.y - 3,
                    rectangle.w + 6, rectangle.h + 6), 5, 2, nk_rgb(240, 240, 210));
                nk_draw_text(commands, nk_rect(rectangle.x, rectangle.y + rectangle.h + 4, 140, 18), entity.name.c_str(),
                             static_cast<int>(entity.name.size()), font, nk_rgb(0, 0, 0), nk_rgb(255, 255, 255));
                if (nk_input_is_mouse_click_in_rect(&ui.input, NK_BUTTON_LEFT, rectangle)) selectedEntity = entity.id;
            }
            for (const auto &text : world.overlayText) {
                overlayFonts.push_back(*font);
                nk_user_font &overlayFont = overlayFonts.back();
                overlayFont.height = static_cast<float>(std::max(1.0, text.size));
                float textWidth = overlayFont.width(overlayFont.userdata, overlayFont.height,
                                                    text.text.c_str(), static_cast<int>(text.text.size()));
                nk_draw_text(commands, nk_rect(bounds.x + text.position.x, bounds.y + text.position.y,
                    textWidth + 1, overlayFont.height), text.text.c_str(), static_cast<int>(text.text.size()),
                    &overlayFont, nk_rgba(0,0,0,0), nk_rgba(static_cast<int>(text.r*255),static_cast<int>(text.g*255),static_cast<int>(text.b*255),static_cast<int>(text.a*255)));
            }
            nk_layout_space_end(&ui);
        }
        nk_end(&ui);
    }
    void inspector(float x, float y, float w, float h) {
        if (nk_begin(&ui, "Inspector", nk_rect(x, y, w, h), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 22, 1);
            nk_label_colored(&ui, tr("INSPECTOR  /  native components", "ИНСПЕКТОР  /  компоненты"), NK_TEXT_LEFT, nk_rgb(255, 255, 255));
            auto *entity = selected();
            if (!entity) {
                nk_layout_row_dynamic(&ui, 30, 1); nk_label(&ui, tr("Select an entity", "Выберите объект"), NK_TEXT_LEFT);
            } else {
                nk_layout_row_dynamic(&ui, 25, 1); nk_label(&ui, entity->name.c_str(), NK_TEXT_LEFT);
                nk_layout_row_dynamic(&ui, 25, 2);
                nk_property_double(&ui, "X", -10000, &entity->transform.position.x, 10000, 1, 1);
                nk_property_double(&ui, "Y", -10000, &entity->transform.position.y, 10000, 1, 1);
                nk_property_double(&ui, "VX", -10000, &entity->body.velocity.x, 10000, 1, 1);
                nk_property_double(&ui, "VY", -10000, &entity->body.velocity.y, 10000, 1, 1);
                nk_property_double(&ui, "W", 1, &entity->sprite.size.x, 4096, 1, 1);
                nk_property_double(&ui, "H", 1, &entity->sprite.size.y, 4096, 1, 1);
                nk_layout_row_dynamic(&ui, 25, 1);
                nk_property_double(&ui, tr("Rotation", "Поворот"), -36000, &entity->transform.rotation, 36000, 0.1, 0.1);
                nk_property_double(&ui, tr("Scale", "Масштаб"), 0.01, &entity->transform.scale, 100, 0.05, 0.05);
                nk_property_double(&ui, tr("Angular V", "Угл. скор."), -1000, &entity->body.angularVelocity, 1000, 0.1, 0.1);
                nk_property_double(&ui, tr("Red", "Красный"), 0, &entity->sprite.r, 1, 0.01, 0.01);
                nk_property_double(&ui, tr("Green", "Зелёный"), 0, &entity->sprite.g, 1, 0.01, 0.01);
                nk_property_double(&ui, tr("Blue", "Синий"), 0, &entity->sprite.b, 1, 0.01, 0.01);
                nk_property_double(&ui, tr("Alpha", "Прозрачность"), 0, &entity->sprite.a, 1, 0.01, 0.01);
                nk_layout_row_dynamic(&ui, 22, 1);
                nk_label(&ui, tr("Texture (PNG/JPG path)", "Текстура (путь PNG/JPG)"), NK_TEXT_LEFT);
                { char path[512]; std::snprintf(path, sizeof(path), "%s", entity->sprite.texture.c_str()); int length = static_cast<int>(std::strlen(path));
                  nk_edit_string(&ui, NK_EDIT_SIMPLE, path, &length, sizeof(path)-1, nk_filter_default); path[length] = 0; entity->sprite.texture = path; }
                nk_layout_row_dynamic(&ui, 26, 1);
                nk_bool active = entity->active, kinematic = entity->body.kinematic;
                if (nk_checkbox_label(&ui, tr("Active", "Активен"), &active)) entity->active = active;
                if (nk_checkbox_label(&ui, tr("Kinematic", "Кинематический"), &kinematic)) entity->body.kinematic = kinematic;
                for (const auto &tag : entity->tags) nk_label(&ui, ("#" + tag).c_str(), NK_TEXT_LEFT);
                for (const auto &component : entity->components) {
                    std::string value = component.first + " = " + std::to_string(component.second);
                    nk_label(&ui, value.c_str(), NK_TEXT_LEFT);
                }
                auto hits = world.collisions(entity->id);
                std::string collision = tr("Collisions: ", "Столкновений: ") + std::to_string(hits.size());
                nk_label(&ui, collision.c_str(), NK_TEXT_LEFT);
                nk_layout_row_dynamic(&ui, 30, 1);
                if (nk_button_label(&ui, tr("Delete entity", "Удалить объект"))) {
                    world.destroy(selectedEntity);
                    selectedEntity = world.entities().empty() ? luac::engine::invalidEntity : world.entities().front().id;
                }
            }
        }
        nk_end(&ui);
    }
    void engineWorkspace(float bodyHeight) {
        float left = std::min(230.0f, width * 0.20f);
        float right = std::min(280.0f, width * 0.24f);
        float middle = std::max(120.0f, width - left - right - 28);
        hierarchy(8, 68, left, bodyHeight);
        inspector(left + 20 + middle, 68, right, bodyHeight);
        float outputHeight = std::min(160.0f, bodyHeight * 0.28f);
        viewport(left + 12, 68, middle, bodyHeight - outputHeight - 8);
        output(left + 12, 68 + bodyHeight - outputHeight, middle, outputHeight);
    }
    void footer() {
        if (nk_begin(&ui, "Status", nk_rect(0, static_cast<float>(height - 28),
                     static_cast<float>(width), 28), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
            nk_layout_row_dynamic(&ui, 18, 1);
            char status[160];
            if (engineMode)
                std::snprintf(status, sizeof(status), tr("ENGINE  |  %zu entities  |  %s  |  F7 editor", "ДВИЖОК  |  объектов: %zu  |  %s  |  F7 editor"), world.count(), world.running() ? tr("playing", "запущен") : tr("paused", "пауза"));
            else
                std::snprintf(status, sizeof(status), tr("%d bytes  |  Vulkan  |  F5 run  F6 check  F7 engine  Ctrl+S save", "%d байт  |  Vulkan  |  F5 run  F6 check  F7 engine  Ctrl+S save"), sourceLength);
            nk_label(&ui, status, NK_TEXT_LEFT);
        }
        nk_end(&ui);
    }
    void settings() {
        if (!settingsOpen) return;
        if (nk_begin(&ui, "Settings panel", nk_rect(std::max(8, width - 300), 66, 280, 460), NK_WINDOW_BORDER)) {
            nk_layout_row_dynamic(&ui, 25, 1);
            nk_label(&ui, tr("Settings", "Настройки"), NK_TEXT_LEFT);
            nk_label(&ui, tr("Language", "Язык"), NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ui, 30, 2);
            if (nk_option_label(&ui, "English", !russian) && russian) {
                russian = false;
                if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "language=en\n");
            }
            if (nk_option_label(&ui, "Русский", russian) && !russian) {
                russian = true;
                if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "language=ru\n");
            }
            nk_layout_row_dynamic(&ui, 22, 1);
            nk_label(&ui, tr("Accent color", "Цвет темы"), NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ui, 215, 1);
            if (std::getenv("LUAC_UI_TRACE")) {
                static bool reported = false;
                if (!reported) {
                    struct nk_rect bounds = nk_widget_bounds(&ui);
                    std::fprintf(stderr, "wheel=%.0f,%.0f,%.0f,%.0f\n", bounds.x, bounds.y, bounds.w, bounds.h);
                    reported = true;
                }
            }
            if (themeWheel.draw(&ui)) {
                themeDirty = true;
                formatColorHex(themeWheel.color(), themeHex);
                themeHexLength = 7;
                themeHexInvalid = false;
                applyTheme();
                if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "theme=changed\n");
            }
            nk_layout_row_begin(&ui, NK_DYNAMIC, 26, 2);
            nk_layout_row_push(&ui, 0.62f);
            nk_edit_string(&ui, NK_EDIT_FIELD, themeHex, &themeHexLength, sizeof(themeHex), nk_filter_default);
            themeHex[themeHexLength] = 0;
            nk_layout_row_push(&ui, 0.38f);
            if (nk_button_label(&ui, tr("Apply", "Применить"))) {
                nk_color color;
                if (parseColorHex(themeHex, themeHexLength, color)) {
                    themeWheel.setColor(color);
                    formatColorHex(color, themeHex);
                    themeHexLength = 7;
                    themeHexInvalid = false;
                    themeDirty = true;
                    applyTheme();
                    if (std::getenv("LUAC_UI_TRACE")) std::fprintf(stderr, "theme=hex\n");
                } else themeHexInvalid = true;
            }
            nk_layout_row_end(&ui);
            nk_layout_row_dynamic(&ui, 24, 1);
            struct nk_rect swatch = nk_widget_bounds(&ui);
            nk_label(&ui, themeHexInvalid ? tr("Use #RRGGBB", "Введите #RRGGBB") : tr("Preview", "Образец"), NK_TEXT_LEFT);
            nk_fill_rect(nk_window_get_canvas(&ui), nk_rect(swatch.x + swatch.w - 60, swatch.y + 2, 54, 20), 4, themeWheel.color());
            nk_layout_row_dynamic(&ui, 26, 1);
            if (nk_button_label(&ui, tr("Close", "Закрыть"))) { saveTheme(); settingsOpen = false; }
            if (themeDirty && !ui.input.mouse.buttons[NK_BUTTON_LEFT].down) saveTheme();
        }
        nk_end(&ui);
    }
    void draw() {
        toolbar();
        float bodyHeight = static_cast<float>(std::max(40, height - 96));
        if (engineMode) engineWorkspace(bodyHeight);
        else if (page == Page::Projects) projectList(bodyHeight);
        else if (page == Page::Files) fileList(bodyHeight);
        else {
            float consoleHeight = std::min(170.0f, std::max(110.0f, bodyHeight * 0.25f));
            editor(16, 68, width - 32.0f, bodyHeight - consoleHeight - 8);
            output(16, 68 + bodyHeight - consoleHeight, width - 32.0f, consoleHeight);
        }
        footer();
        settings();
        projectDialog();
        canvas.render(ui, width, height);
        renderer.render(canvas.vertices, canvas.draws, width, height);
        nk_clear(&ui);
        overlayFonts.clear();
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
    a->world.input.setKey(keyCode, down);
    if (keyCode == GLFW_KEY_LEFT_CONTROL || keyCode == GLFW_KEY_RIGHT_CONTROL) {
        a->controlDown = down; nk_input_key(&a->ui, NK_KEY_CTRL, down); return;
    }
    if (keyCode == GLFW_KEY_LEFT_SHIFT || keyCode == GLFW_KEY_RIGHT_SHIFT) {
        nk_input_key(&a->ui, NK_KEY_SHIFT, down); return;
    }
    if (keyCode == GLFW_KEY_LEFT_ALT || keyCode == GLFW_KEY_RIGHT_ALT) {
        nk_input_key(&a->ui, NK_KEY_ALT, down); return;
    }
    if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && keyCode == GLFW_KEY_S && a->page == App::Page::Editor) { a->save(); return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_F5 && a->page != App::Page::Projects) { a->run(); return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_F6 && a->page == App::Page::Editor) { a->check(); return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_F7) { a->engineMode = !a->engineMode; return; }
    if (action == GLFW_PRESS && keyCode == GLFW_KEY_SPACE && a->engineMode) {
        if (a->world.running()) a->world.pause(); else a->world.play(); return;
    }
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
    if (keyCode >= 0 && keyCode <= GLFW_KEY_LAST) {
        if (action == GLFW_RELEASE) {
            mapped = a->pressedKeys[keyCode];
            a->pressedKeys[keyCode] = NK_KEY_NONE;
        } else if (action == GLFW_PRESS) {
            a->pressedKeys[keyCode] = mapped;
        }
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
    a->world.input.setMouseButton(button, action != GLFW_RELEASE);
    nk_input_button(&a->ui, mapped, static_cast<int>(a->mouseX), static_cast<int>(a->mouseY),
                    action != GLFW_RELEASE);
}
void mouseMove(GLFWwindow *window, double x, double y) { App *a=app(window); a->pointer(x,y); a->world.input.setMousePosition({a->mouseX,a->mouseY}); }
void scroll(GLFWwindow *window, double x, double y) {
    app(window)->world.input.setWheel({x,y});
    nk_input_scroll(&app(window)->ui, nk_vec2(static_cast<float>(x), static_cast<float>(y)));
}
}

int main(int argc, char **argv) {
    // The package check runs before opening a window, including on headless CI.
    if (argc == 2 && std::strcmp(argv[1], "--check-dependencies") == 0) {
        std::cout << "LuaC Studio native dependencies loaded\n";
        return 0;
    }
    if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return 1; }
    if (!glfwVulkanSupported()) { std::cerr << "Vulkan is not supported by this system\n"; glfwTerminate(); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1280, 800, "LuaC Studio - Nuklear + Vulkan", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwSetWindowSizeLimits(window, 1000, 500, GLFW_DONT_CARE, GLFW_DONT_CARE);
    App application;
    application.window = window;
    application.compiler = std::getenv("LUAC_COMPILER") ? std::getenv("LUAC_COMPILER") :
        std::filesystem::absolute("./luac").u8string();
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
        application.initializeProjects();
        application.setSource("-- LuaC Studio / Nuklear immediate UI\nlocal function greet(name)\n  return 'Hello, ' .. name\nend\n\nprint(greet('Vulkan'))\n");
        if (argc > 1) { application.load(argv[1]); application.page = App::Page::Editor; }
        application.renderer.initialize(window, shaders ? shaders : "build");
        application.renderer.updateTexture(1, application.canvas.atlas().width(),
            application.canvas.atlas().height(), application.canvas.atlas().pixels());
        application.canvas.atlas().finish();
        double previousTime = glfwGetTime();
        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime();
            application.world.input.beginFrame();
            nk_input_begin(&application.ui);
            glfwPollEvents();
            nk_input_end(&application.ui);
            if (application.engineMode) application.world.advance(currentTime - previousTime);
            previousTime = currentTime;
            glfwGetFramebufferSize(window, &application.width, &application.height);
            if (application.width > 0 && application.height > 0) application.draw();
            else { nk_clear(&application.ui); glfwWaitEvents(); }
        }
        application.saveTheme();
        nk_free(&application.ui);
        application.renderer.shutdown();
    } catch (const std::exception &error) {
        std::cerr << "LuaC Studio: " << error.what() << '\n';
        application.saveTheme();
        nk_free(&application.ui);
        application.renderer.shutdown();
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
