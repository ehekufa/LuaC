package com.luac.desktop;

import java.io.IOException;
import java.nio.file.*;
import java.util.concurrent.*;
import javax.swing.SwingUtilities;

public final class DesktopTest {
    static void check(boolean value, String message) { if (!value) throw new AssertionError(message); }
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
            for (String bad : new String[]{"../escape", "bad/name", "", "CON"}) {
                try { store.createFile(dir, bad); throw new AssertionError("accepted invalid name: " + bad); } catch (IOException expected) { }
            }
            try { store.children(temp); throw new AssertionError("allowed path outside root"); } catch (IOException expected) { }
            try { store.createFile(dir, "other2.lua"); throw new AssertionError("overwrote file"); } catch (IOException expected) { }
            store.save(project.resolve("main.lua"), "local g = require('scripts.greeting')\nprint(g.message)\n");
            Compiler compiler = new Compiler(home);
            check(run(compiler, project, "--jit", Paths.get("main.lua"), false).contains("Hello from LuaC!"), "native output missing");
            run(compiler, project, "--check", project.resolve("main.lua"), false);
            store.save(project.resolve("main.lua"), "print('BEGIN')\nwhile true do end\n");
            run(compiler, project, "--jit", Paths.get("main.lua"), true);
            store.save(project.resolve("main.lua"), "print('GUI RUN OK')\n");
            if (args.length > 1 && args[1].equals("--gui")) {
                Studio[] ui = new Studio[1];
                SwingUtilities.invokeAndWait(() -> { try { ui[0] = new Studio(temp.resolve("projects"), home); ui[0].setVisible(true); ui[0].showFiles(project); ui[0].openFile(project.resolve("main.lua")); ui[0].editor.append("-- Edited from Java\n"); check(ui[0].save(), "GUI save failed"); ui[0].runProject(); } catch (Exception e) { throw new RuntimeException(e); } });
                long end = System.currentTimeMillis() + 60000;
                while (System.currentTimeMillis() < end) {
                    boolean[] done = new boolean[1]; SwingUtilities.invokeAndWait(() -> done[0] = !ui[0].running);
                    if (done[0]) break; Thread.sleep(100);
                }
                SwingUtilities.invokeAndWait(() -> {
                    check(!ui[0].running, "GUI run timeout"); check(ui[0].output.getText().contains("GUI RUN OK"), "GUI output " + ui[0].output.getText());
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
