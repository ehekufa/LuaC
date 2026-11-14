package com.luac.studio;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

final class ProjectStore {
    final File root;
    private final Context context;

    ProjectStore(Context context) {
        this.context = context.getApplicationContext();
        root = new File(context.getFilesDir(), "projects");
        if (!root.exists()) root.mkdirs();
    }

    // Allows filesystem operations to be tested without an Android Context.
    ProjectStore(File root) {
        this.context = null;
        this.root = root;
        if (!root.exists()) root.mkdirs();
    }

    List<File> projects() {
        ArrayList<File> result = new ArrayList<>();
        File[] files = root.listFiles();
        if (files != null) for (File file : files) if (file.isDirectory()) result.add(file);
        result.sort((a, b) -> a.getName().compareToIgnoreCase(b.getName()));
        return result;
    }

    File createProject(String requested) throws IOException {
        String name = safeName(requested, "Project");
        File project = unique(root, name);
        if (!new File(project, "scripts").mkdirs() || !new File(project, "assets").mkdirs()) throw new IOException("Cannot create project folders");
        write(new File(project, "main.lua"),
            "-- LuaC project: main.lua is the entry point\n" +
            "local player = require('scripts.player')\n\n" +
            "world = engine.world()\n" +
            "engine.set_gravity(world, 0, 420)\n\n" +
            "local floor = engine.create(world, 'Floor', 0, 180)\n" +
            "engine.set_size(floor, 520, 32)\n" +
            "engine.set_color(floor, 0.12, 0.25, 0.30, 1)\n" +
            "engine.set_body(floor, {kinematic = true, solid = true})\n\n" +
            "local texture, textureError = engine.load_texture('assets/luac_logo.png')\n" +
            "assert(texture, textureError)\n" +
            "hero = player.create(world, texture)\n" +
            "engine.text(world, 'LuaC • multi-file project', 18, 28, 22)\n" +
            "engine.text(world, 'Touch the scene to move the sprite', 18, 56, 15)\n\n" +
            "function update(dt)\n" +
            "  player.update(hero, dt)\n" +
            "  local x, y = engine.touch_pos(world, 0)\n" +
            "  if x then engine.set_velocity(hero, (x - hero.x) * 5, hero.vy) end\n" +
            "end\n\n" +
            "print('Project started:', projectName or 'LuaC')\n" +
            "print('main.lua + scripts/player.lua + PNG asset')\n");
        write(new File(project, "scripts/player.lua"),
            "local player = {}\n\n" +
            "function player.create(world, texture)\n" +
            "  local entity = engine.create(world, 'Player', 0, -100)\n" +
            "  engine.set_sprite(entity, texture, 64, 64)\n" +
            "  engine.set_velocity(entity, 90, 0)\n" +
            "  engine.set_body(entity, {solid = true, restitution = 0.72, damping = 0.02})\n" +
            "  return entity\n" +
            "end\n\n" +
            "function player.update(entity, dt)\n" +
            "  if entity.x < -230 or entity.x > 230 then\n" +
            "    engine.set_velocity(entity, -entity.vx, entity.vy)\n" +
            "  end\n" +
            "end\n\n" +
            "return player\n");
        write(new File(project, "project.lua"),
            "-- Optional project settings are ordinary Lua files.\n" +
            "projectName = " + luaString(project.getName()) + "\n");
        // Load optional settings before main through a small generated prelude.
        File main = new File(project, "main.lua");
        String source = read(main);
        write(main, "require('project')\n" + source);
        write(new File(project, "assets/README.txt"),
            "Import PNG or JPG files with ASSET+. Paths are relative to the project:\n" +
            "engine.load_texture('assets/luac_logo.png')\n");
        copyLogo(new File(project, "assets/luac_logo.png"));
        return project;
    }

    private String luaString(String value) { return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"; }

    private void copyLogo(File destination) throws IOException {
        try (InputStream input = context.getResources().openRawResource(R.drawable.luac_logo)) {
            copy(input, destination);
        }
    }

    List<File> children(File directory) {
        ArrayList<File> result = new ArrayList<>();
        File[] files = directory.listFiles();
        if (files != null) result.addAll(Arrays.asList(files));
        result.sort(Comparator.comparing(File::isFile).thenComparing((File file) -> file.getName().toLowerCase()));
        return result;
    }

    File createFile(File directory, String requested) throws IOException {
        String name = safeName(requested, "script.lua");
        if (!name.contains(".")) name += ".lua";
        File file = checkedChild(directory, name);
        if (file.exists()) throw new IOException("File already exists");
        write(file, "-- " + name + "\n\n");
        return file;
    }

    File createFolder(File directory, String requested) throws IOException {
        File folder = checkedChild(directory, safeName(requested, "folder"));
        if (!folder.mkdir()) throw new IOException("Cannot create folder");
        return folder;
    }

    File rename(File file, String requested) throws IOException {
        if (!file.exists() || file.equals(root) || !file.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator))
            throw new IOException("File is outside the projects folder or does not exist");
        if (requested == null || requested.trim().isEmpty()) throw new IOException("Name cannot be empty");
        String name = requested.trim();
        if (name.equals(file.getName())) return file;
        File target = checkedChild(file.getParentFile(), name);
        if (target.exists()) throw new IOException("A file or folder with this name already exists");
        if (!file.renameTo(target)) throw new IOException("Cannot rename " + file.getName());
        return target;
    }

    int fileCount(File directory) {
        int count = 0;
        File[] children = directory.listFiles();
        if (children != null) for (File child : children) {
            if (child.isDirectory()) count += fileCount(child);
            else count++;
        }
        return count;
    }

    void exportProject(File project, OutputStream output) throws IOException {
        if (project == null || project.getParentFile() == null ||
            !project.getParentFile().getCanonicalFile().equals(root.getCanonicalFile()))
            throw new IOException("Project is outside the projects folder");
        ProjectArchive.write(project, output);
    }

    File importProject(InputStream input) throws IOException {
        ProjectArchive.Document document = ProjectArchive.read(input);
        // Build outside the project list; an incomplete import must never appear as a project.
        File project = Files.createTempDirectory(root.getParentFile().toPath(), ".luac-import-").toFile();
        try {
            for (String path : document.directories) {
                File directory = resolve(project, path);
                if (!directory.isDirectory() && !directory.mkdirs())
                    throw new IOException("Cannot create directory: " + path);
            }
            for (ProjectArchive.Entry entry : document.files) {
                File file = resolve(project, entry.path);
                File parent = file.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs())
                    throw new IOException("Cannot create directory for " + entry.path);
                try (FileOutputStream output = new FileOutputStream(file)) { output.write(entry.bytes); }
            }
            File destination = unique(root, safeName(document.name, "Project"));
            Files.move(project.toPath(), destination.toPath());
            return destination;
        } catch (IOException | RuntimeException error) {
            try { delete(project); } catch (IOException cleanup) { error.addSuppressed(cleanup); }
            throw error;
        }
    }

    File importFile(File directory, String requested, InputStream input) throws IOException {
        String name = safeName(requested, "asset.bin");
        if (!directory.exists() && !directory.mkdirs()) throw new IOException("Cannot create assets folder");
        File destination = unique(directory, name);
        copy(input, destination);
        return destination;
    }

    String read(File file) throws IOException {
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] data = new byte[(int) Math.min(file.length(), 4 * 1024 * 1024)];
            int offset = 0, count;
            while (offset < data.length && (count = input.read(data, offset, data.length - offset)) >= 0) offset += count;
            return new String(data, 0, offset, StandardCharsets.UTF_8);
        }
    }

    void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) throw new IOException("Cannot create parent folder");
        try (FileOutputStream output = new FileOutputStream(file)) { output.write(text.getBytes(StandardCharsets.UTF_8)); }
    }

    void delete(File file) throws IOException {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) delete(child);
        }
        if (!file.delete()) throw new IOException("Cannot delete " + file.getName());
    }

    File resolve(File project, String relative) throws IOException {
        File result = new File(project, relative);
        String base = project.getCanonicalPath() + File.separator;
        if (!result.getCanonicalPath().startsWith(base)) throw new IOException("Path leaves the project");
        return result;
    }

    private File checkedChild(File directory, String name) throws IOException {
        if (name.contains("/") || name.contains("\\") || name.equals(".") || name.equals("..")) throw new IOException("Invalid name");
        return resolve(directory, name);
    }

    private static String safeName(String requested, String fallback) {
        String name = requested == null ? "" : requested.trim().replaceAll("[\\\\/:*?\"<>|]", "_");
        return name.isEmpty() ? fallback : name;
    }

    private static File unique(File directory, String name) {
        File result = new File(directory, name);
        if (!result.exists()) return result;
        int dot = name.lastIndexOf('.');
        String base = dot > 0 ? name.substring(0, dot) : name;
        String extension = dot > 0 ? name.substring(dot) : "";
        for (int index = 2; ; ++index) {
            result = new File(directory, base + " " + index + extension);
            if (!result.exists()) return result;
        }
    }

    private static void copy(InputStream input, File destination) throws IOException {
        try (FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) >= 0) output.write(buffer, 0, count);
        }
    }
}
