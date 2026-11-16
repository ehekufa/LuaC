package com.luac.desktop;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;

final class ProjectStore {
    final Path root;
    ProjectStore(Path root) throws IOException {
        Files.createDirectories(root);
        this.root = root.toRealPath();
    }
    private Path checked(Path path) throws IOException {
        Path absolute = path.toAbsolutePath().normalize();
        if (!absolute.startsWith(root)) throw new IOException("Path is outside the projects folder");
        Path part = root;
        for (Path name : root.relativize(absolute)) {
            part = part.resolve(name);
            if (Files.isSymbolicLink(part)) throw new IOException("Symbolic links are not supported");
        }
        Path real = absolute.toRealPath();
        if (!real.startsWith(root)) throw new IOException("Path is outside the projects folder");
        return real;
    }
    private String valid(String name) throws IOException {
        if (name == null || name.isBlank() || name.length() > 200 || name.equals(".") || name.equals("..") ||
                name.matches(".*[\\\\/:*?\"<>|\\p{Cntrl}].*") || name.endsWith(".") || name.endsWith(" "))
            throw new IOException("Invalid file or project name");
        // These names are reserved by Windows, even with extensions.
        if (name.matches("(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(\\..*)?")) throw new IOException("Reserved file name");
        return name;
    }
    List<Path> projects() throws IOException { return children(root).stream().filter(Files::isDirectory).collect(Collectors.toList()); }
    List<Path> children(Path dir) throws IOException {
        try (Stream<Path> stream = Files.list(checked(dir))) {
            return stream.filter(p -> !Files.isSymbolicLink(p))
                .filter(p -> Files.isDirectory(p, LinkOption.NOFOLLOW_LINKS) || Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS))
                .sorted(Comparator.comparing((Path p) -> !Files.isDirectory(p)).thenComparing(p -> p.getFileName().toString(), String.CASE_INSENSITIVE_ORDER))
                .collect(Collectors.toList());
        }
    }
    Path createProject(String name) throws IOException {
        Path project = root.resolve(valid(name));
        Files.createDirectory(project);
        try {
            Files.createDirectory(project.resolve("scripts"));
            Files.createDirectory(project.resolve("assets"));
            Files.writeString(project.resolve("main.lua"), "print('Hello from LuaC!')\n", StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
            Files.writeString(project.resolve("scripts/greeting.lua"), "return {message = 'Hello from LuaC!'}\n", StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
            return project;
        } catch (IOException e) { try { delete(project); } catch (IOException cleanup) { e.addSuppressed(cleanup); } throw e; }
    }
    Path createFile(Path dir, String name) throws IOException {
        name = valid(name); if (!name.contains(".")) name += ".lua";
        Path path = checked(dir).resolve(name);
        Files.writeString(path, "-- New Lua file\n", StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW);
        return path;
    }
    Path createFolder(Path dir, String name) throws IOException { return Files.createDirectory(checked(dir).resolve(valid(name))); }
    void importFile(Path dir, Path source) throws IOException {
        Path target = checked(dir).resolve(valid(source.getFileName().toString()));
        if (!Files.isRegularFile(source, LinkOption.NOFOLLOW_LINKS)) throw new IOException("Not a regular file");
        Files.copy(source, target);
    }
    Path savePainting(Path project, String name, java.awt.image.BufferedImage image) throws IOException {
        project = checked(project);
        if (!project.getParent().equals(root)) throw new IOException("Not a project");
        name = valid(name); if (!name.toLowerCase(Locale.ROOT).endsWith(".png")) name += ".png";
        Path assets = project.resolve("assets");
        if (!Files.exists(assets, LinkOption.NOFOLLOW_LINKS)) Files.createDirectory(assets);
        assets = checked(assets);
        java.io.ByteArrayOutputStream buffer = new java.io.ByteArrayOutputStream();
        if (!javax.imageio.ImageIO.write(image, "png", buffer)) throw new IOException("PNG encoder unavailable");
        Path result = assets.resolve(name); Files.write(result, buffer.toByteArray(), StandardOpenOption.CREATE_NEW); return result;
    }
    void exportProject(Path project, Path target) throws IOException {
        project = checked(project);
        if (!project.getParent().equals(root) || !Files.isDirectory(project)) throw new IOException("Not a project");
        Path absolute = target.toAbsolutePath();
        Path tmp = Files.createTempFile(absolute.getParent(), ".luac-export-", ".tmp");
        try {
            try (java.io.OutputStream out = Files.newOutputStream(tmp)) { ProjectArchive.write(project.toFile(), out); }
            try { Files.move(tmp, absolute, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (AtomicMoveNotSupportedException e) { Files.move(tmp, absolute, StandardCopyOption.REPLACE_EXISTING); }
        } finally { Files.deleteIfExists(tmp); }
    }
    Path importProject(Path archive) throws IOException {
        ProjectArchive.Document document;
        try (java.io.InputStream in = Files.newInputStream(archive)) { document = ProjectArchive.read(in); }
        return installDocument(document);
    }
    Path installDocument(ProjectArchive.Document document) throws IOException {
        String base = valid(document.name);
        // Validate every component before creating any project data.
        for (String dir : document.directories) archivePath(dir);
        for (ProjectArchive.Entry file : document.files) archivePath(file.path);
        Path stage = Files.createTempDirectory(root, ".luac-import-");
        try {
            for (String dir : document.directories) Files.createDirectories(stage.resolve(archivePath(dir)));
            for (ProjectArchive.Entry entry : document.files) {
                Path target = stage.resolve(archivePath(entry.path));
                Files.createDirectories(target.getParent());
                Files.write(target, entry.bytes, StandardOpenOption.CREATE_NEW);
            }
            for (int i = 0; i < 10000; ++i) {
                Path destination = root.resolve(i == 0 ? base : base + " (" + (i + 1) + ")");
                try { return Files.move(stage, destination); } catch (FileAlreadyExistsException e) { /* choose a fresh name */ }
            }
            throw new IOException("Too many projects with this name");
        } finally { if (Files.exists(stage)) delete(stage); }
    }
    private Path archivePath(String path) throws IOException {
        Path result = Paths.get("");
        for (String part : path.split("/", -1)) result = result.resolve(valid(part));
        return result;
    }
    Path rename(Path path, String name) throws IOException {
        path = checked(path);
        if (path.equals(root)) throw new IOException("Cannot rename the projects folder");
        Path target = path.resolveSibling(valid(name));
        if (path.equals(target)) return path;
        return Files.move(path, target);
    }
    void delete(Path path) throws IOException {
        path = checked(path);
        if (path.equals(root)) throw new IOException("Cannot delete the projects folder");
        Files.walkFileTree(path, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException { Files.delete(file); return FileVisitResult.CONTINUE; }
            @Override public FileVisitResult postVisitDirectory(Path dir, IOException e) throws IOException { if (e != null) throw e; Files.delete(dir); return FileVisitResult.CONTINUE; }
        });
    }
    String read(Path file) throws IOException {
        file = checked(file);
        if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS)) throw new IOException("Not a regular file");
        if (Files.size(file) > 2 * 1024 * 1024) throw new IOException("File exceeds 2 MiB");
        return Files.readString(file, StandardCharsets.UTF_8);
    }
    void save(Path file, String text) throws IOException {
        file = checked(file);
        if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS)) throw new IOException("Not a regular file");
        Path tmp = Files.createTempFile(file.getParent(), ".luac-save-", ".tmp");
        try {
            Files.writeString(tmp, text, StandardCharsets.UTF_8);
            try { Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (AtomicMoveNotSupportedException e) { Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING); }
        } finally { Files.deleteIfExists(tmp); }
    }
}
