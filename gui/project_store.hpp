#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

/* Desktop projects are normal folders; existing Lua files are never converted. */
class ProjectStore {
public:
    using Path = std::filesystem::path;
    explicit ProjectStore(Path root) : root_(std::move(root)) {
        std::filesystem::create_directories(root_);
        root_ = std::filesystem::canonical(root_);
    }
    const Path &root() const { return root_; }
    std::vector<Path> projects() const { return children(root_, true); }
    std::vector<Path> files(const Path &directory) const {
        inside(directory);
        return children(directory, false);
    }
    Path createProject(const std::string &name) const {
        Path destination = root_ / validName(name);
        if (!std::filesystem::create_directory(destination)) throw std::runtime_error("Project already exists");
        try {
            std::filesystem::create_directory(destination / "scripts");
            std::filesystem::create_directory(destination / "assets");
            writeNew(destination / "main.lua", "print('Hello from LuaC!')\n");
            writeNew(destination / "scripts" / "greeting.lua", "return { message = 'Hello from LuaC!' }\n");
            writeNew(destination / "assets" / "README.txt", "Place project images and other assets here.\n");
        } catch (...) { std::filesystem::remove_all(destination); throw; }
        return destination;
    }
    Path createFile(const Path &directory, const std::string &name) const {
        inside(directory);
        Path part = validName(name);
        if (!part.has_extension()) part += ".lua";
        Path path = directory / part;
        if (std::filesystem::exists(path) || std::filesystem::is_symlink(path)) throw std::runtime_error("File already exists");
        writeNew(path, "-- New Lua file\n");
        return path;
    }
    Path createFolder(const Path &directory, const std::string &name) const {
        inside(directory);
        Path path = directory / validName(name);
        if (!std::filesystem::create_directory(path)) throw std::runtime_error("Folder already exists");
        return path;
    }
    Path rename(const Path &path, const std::string &name) const {
        inside(path);
        if (std::filesystem::equivalent(path, root_)) throw std::runtime_error("Cannot rename project root");
        Path destination = path.parent_path() / validName(name);
        if (path == destination) return path;
        if (std::filesystem::exists(destination) || std::filesystem::is_symlink(destination))
            throw std::runtime_error("A file or folder already exists with that name");
        std::filesystem::rename(path, destination);
        return destination;
    }
    void remove(const Path &path) const {
        inside(path);
        if (std::filesystem::equivalent(path, root_)) throw std::runtime_error("Cannot delete project root");
        if (!std::filesystem::remove_all(path)) throw std::runtime_error("Cannot delete item");
    }
private:
    Path root_;
    static Path validName(const std::string &name) {
        if (name.empty() || name == "." || name == ".." || name.size() > 200 ||
            name.find_first_of("/\\:*?\"<>|\r\n") != std::string::npos ||
            name.find('\0') != std::string::npos || name.back() == ' ' || name.back() == '.')
            throw std::runtime_error("Enter a valid name");
        return std::filesystem::u8path(name);
    }
    void inside(const Path &path) const {
        if (std::filesystem::is_symlink(path)) throw std::runtime_error("Symbolic links are not supported");
        Path resolved = std::filesystem::canonical(path);
        Path relative = resolved.lexically_relative(root_);
        if (relative.empty() || (!relative.empty() && *relative.begin() == ".."))
            throw std::runtime_error("Path is outside the projects folder");
    }
    static void writeNew(const Path &path, const std::string &text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text) || !file.flush()) throw std::runtime_error("Cannot write project file");
    }
    static std::vector<Path> children(const Path &directory, bool directoriesOnly) {
        std::vector<Path> result;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_symlink()) continue;
            if (directoriesOnly && !entry.is_directory()) continue;
            if (!directoriesOnly && !entry.is_directory() && !entry.is_regular_file()) continue;
            result.push_back(entry.path());
        }
        std::sort(result.begin(), result.end(), [](const Path &a, const Path &b) {
            bool ad = std::filesystem::is_directory(a), bd = std::filesystem::is_directory(b);
            return ad != bd ? ad : a.filename().u8string() < b.filename().u8string();
        });
        return result;
    }
};
