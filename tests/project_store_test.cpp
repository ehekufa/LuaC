#include "project_store.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    fs::path temp = fs::temp_directory_path() / ("luac-project-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp);
    try {
        ProjectStore store(temp / "projects");
        auto project = store.createProject("Example");
        assert(fs::is_regular_file(project / "main.lua"));
        assert(fs::is_regular_file(project / "scripts/greeting.lua"));
        assert(fs::is_directory(project / "assets"));
        assert(store.projects().size() == 1);
        auto dir = store.createFolder(project, "extra");
        for (int i = 0; i < 30; ++i) store.createFile(dir, "module" + std::to_string(i));
        assert(store.files(dir).size() == 30);
        auto file = dir / "module0.lua";
        auto renamed = store.rename(file, "changed.lua");
        assert(fs::exists(renamed) && !fs::exists(file));
        for (const char *bad : {"", ".", "..", "../outside", "x/y", "x\\y"}) {
            bool rejected = false;
            try { store.createFile(dir, bad); } catch (const std::exception &) { rejected = true; }
            assert(rejected);
        }
        bool rejected = false;
        try { store.createFile(temp, "outside"); } catch (const std::exception &) { rejected = true; }
        assert(rejected);
        rejected = false;
        try { store.rename(renamed, "module1.lua"); } catch (const std::exception &) { rejected = true; }
        assert(rejected && fs::exists(renamed));
        store.remove(dir);
        assert(!fs::exists(dir));
        assert(fs::exists(project / "main.lua"));
        fs::remove_all(temp);
        std::cout << "Desktop multi-file project store: OK\n";
    } catch (...) { fs::remove_all(temp); throw; }
}
