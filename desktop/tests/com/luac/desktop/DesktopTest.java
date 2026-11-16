package com.luac.desktop;

import java.io.IOException;
import java.nio.file.*;
import java.util.concurrent.*;
import javax.swing.*;
import java.awt.*;
import java.awt.event.MouseEvent;

public final class DesktopTest {
    static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
    static Component named(Component root, String name) {
        if (name.equals(root.getName())) return root;
        if (root instanceof Container) for (Component child : ((Container)root).getComponents()) {
            Component found = named(child, name); if (found != null) return found;
        }
        return null;
    }
    static JLabel label(Component root, String text) {
        if (root instanceof JLabel && text.equals(((JLabel)root).getText())) return (JLabel)root;
        if (root instanceof Container) for (Component child : ((Container)root).getComponents()) {
            JLabel found = label(child, text); if (found != null) return found;
        }
        return null;
    }
    static void capture(Studio studio, String name) {
        String directory = System.getenv("LUAC_UI_SCREENSHOTS"); if (directory == null) return;
        try {
            studio.validate();
            java.awt.image.BufferedImage image = new java.awt.image.BufferedImage(studio.getWidth(), studio.getHeight(), java.awt.image.BufferedImage.TYPE_INT_RGB);
            Graphics2D g = image.createGraphics(); studio.printAll(g); g.dispose();
            javax.imageio.ImageIO.write(image, "png", Paths.get(directory, name + ".png").toFile());
        } catch (IOException e) { throw new RuntimeException(e); }
    }
    static String run(Compiler compiler, Path project, String mode, Path source, boolean stop) throws Exception {
        CountDownLatch completed = new CountDownLatch(1), started = new CountDownLatch(1);
        StringBuffer output = new StringBuffer(); int[] code = {-999};
        compiler.start(project, mode, source, text -> { output.append(text); if (output.toString().contains("BEGIN")) started.countDown(); }, value -> { code[0] = value; completed.countDown(); });
        if (stop) { check(started.await(30, TimeUnit.SECONDS), "program did not start: " + output); compiler.stop(); }
        check(completed.await(60, TimeUnit.SECONDS), "compiler timed out: " + output);
        if (!stop) check(code[0] == 0, "compiler failed " + code[0] + ": " + output);
        return output.toString();
    }
    public static void main(String[] args) throws Exception {
        Path home = Paths.get(args[0]).toAbsolutePath();
        Path temp = Files.createTempDirectory("LuaC desktop test with spaces ");
        try {
            ProjectStore store = new ProjectStore(temp.resolve("projects"));
            Path project = store.createProject("Example");
            check(Files.exists(project.resolve("main.lua")), "missing main");
            Path dir = store.createFolder(project, "modules");
            Path file = store.createFile(dir, "other");
            store.save(file, "return 123\n"); check(store.read(file).equals("return 123\n"), "save/read");
            Path renamed = store.rename(file, "other2.lua");
            check(Files.exists(renamed) && !Files.exists(file), "rename");
            Files.write(project.resolve("assets/picture.bin"), new byte[]{0, 1, -1});
            Path archive = temp.resolve("project.luac"); store.exportProject(project, archive);
            Path imported = store.importProject(archive);
            check(!project.equals(imported) && Files.isDirectory(imported.resolve("scripts")), "import renamed project");
            check(java.util.Arrays.equals(Files.readAllBytes(imported.resolve("assets/picture.bin")), new byte[]{0, 1, -1}), "archive binary data");
            store.delete(imported);
            PaintCanvas canvas = new PaintCanvas(); canvas.setSize(512, 512);
            canvas.dispatchEvent(new MouseEvent(canvas, MouseEvent.MOUSE_PRESSED, System.currentTimeMillis(), 0, 40, 40, 1, false));
            check(canvas.dirty && (canvas.image.getRGB(40, 40) >>> 24) != 0, "paint brush");
            Path painting = store.savePainting(project, "drawing", canvas.image);
            check(javax.imageio.ImageIO.read(painting.toFile()).getRGB(0, 0) == 0, "PNG transparency");
            try { store.savePainting(project, "drawing", canvas.image); throw new AssertionError("overwrote painting"); } catch (IOException expected) { }
            canvas.undo(); check(canvas.image.getRGB(40, 40) == 0, "paint undo");
            Files.writeString(archive, "{\"format\":\"luac-project\",\"version\":1,\"name\":\"Unsafe\",\"directories\":[],\"files\":[{\"path\":\"../escape\",\"data\":\"QQ==\"}]}");
            try { store.importProject(archive); throw new AssertionError("accepted unsafe archive"); } catch (IOException expected) { }
            check(!Files.exists(temp.resolve("escape")) && !Files.exists(store.root.resolve("Unsafe")), "unsafe archive wrote files");
            for (String bad : new String[]{"../escape", "bad/name", "", "CON"}) {
                try { store.createFile(dir, bad); throw new AssertionError("accepted invalid name: " + bad); } catch (IOException expected) { }
            }
            try { store.children(temp); throw new AssertionError("allowed path outside root"); } catch (IOException expected) { }
            try { store.createFile(dir, "other2.lua"); throw new AssertionError("overwrote file"); } catch (IOException expected) { }
            store.save(project.resolve("main.lua"), "local g = require('scripts.greeting')\nprint(g.message)\n");
            Compiler compiler = new Compiler(home);
            String nativeOutput = run(compiler, project, "--jit", Paths.get("main.lua"), false);
            check(nativeOutput.contains("Hello from LuaC!"), "native output missing");
            if (System.getenv("LUAC_TRACE") != null && Files.exists(home.resolve("gcc/bin/gcc.exe")))
                check(nativeOutput.contains("gcc.exe"), "bundled GCC was not used: " + nativeOutput);
            run(compiler, project, "--check", project.resolve("main.lua"), false);
            store.save(project.resolve("main.lua"), "print('BEGIN')\nwhile true do end\n");
            run(compiler, project, "--jit", Paths.get("main.lua"), true);
            store.save(project.resolve("main.lua"), "print('GUI RUN OK')\n");
            if (args.length > 1 && args[1].equals("--gui")) {
                Studio[] ui = new Studio[1];
                SwingUtilities.invokeAndWait(() -> { try {
                    ui[0] = new Studio(temp.resolve("projects"), home); ui[0].setVisible(true);
                    check(named(ui[0], "projects-list") != null && named(ui[0], "new-project") != null, "projects screen"); capture(ui[0], "projects");
                    JLabel card = label(ui[0], "Example"); check(card != null, "project card missing");
                    card.dispatchEvent(new MouseEvent(card, MouseEvent.MOUSE_CLICKED, System.currentTimeMillis(), 0, 4, 4, 1, false));
                    check(named(ui[0], "files-list") != null, "files screen"); capture(ui[0], "files");
                    JLabel main = label(ui[0], "main.lua"); check(main != null, "main file card missing");
                    main.dispatchEvent(new MouseEvent(main, MouseEvent.MOUSE_CLICKED, System.currentTimeMillis(), 0, 4, 4, 1, false));
                    check(named(ui[0], "script-editor") != null, "editor screen"); capture(ui[0], "editor");
                    ui[0].editor.append("-- Edited from Java\n"); ((JButton)named(ui[0], "save")).doClick();
                    ((JButton)named(ui[0], "run")).doClick();
                } catch (Exception e) { throw new RuntimeException(e); } });
                long end = System.currentTimeMillis() + 60000;
                while (System.currentTimeMillis() < end) {
                    boolean[] done = new boolean[1]; SwingUtilities.invokeAndWait(() -> done[0] = !ui[0].running);
                    if (done[0]) break; Thread.sleep(100);
                }
                SwingUtilities.invokeAndWait(() -> {
                    check(!ui[0].running, "GUI run timeout"); check(ui[0].output.getText().contains("GUI RUN OK"), "GUI output " + ui[0].output.getText());
                    check(ui[0].stopButton != null && ui[0].stopButton.isEnabled(), "runtime navigation disabled after exit");
                    ui[0].stopButton.doClick(); check(named(ui[0], "script-editor") != null, "return to editor");
                    ui[0].dispose();
                });
                check(store.read(project.resolve("main.lua")).contains("Edited from Java"), "GUI file not saved");
            }
            store.delete(dir); check(!Files.exists(dir) && Files.exists(project.resolve("main.lua")), "delete");
            System.out.println("Java desktop projects, native TinyCC run/check/stop" + (args.length > 1 ? " and GUI" : "") + ": OK");
        } finally {
            Files.walkFileTree(temp, new SimpleFileVisitor<Path>() {
                @Override public FileVisitResult visitFile(Path f, java.nio.file.attribute.BasicFileAttributes a) throws IOException { Files.delete(f); return FileVisitResult.CONTINUE; }
                @Override public FileVisitResult postVisitDirectory(Path d, IOException e) throws IOException { if (e != null) throw e; Files.delete(d); return FileVisitResult.CONTINUE; }
            });
        }
    }
}
