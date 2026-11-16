package com.luac.desktop;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.function.Consumer;

/** Compiles native code through the bundled LuaC and TinyCC without a shell. */
final class Compiler {
    final Path home;
    private Process process;
    Compiler(Path home) { this.home = home.toAbsolutePath().normalize(); }
    boolean windows() { return System.getProperty("os.name").toLowerCase(Locale.ROOT).contains("win"); }
    synchronized void start(Path project, String mode, Path source, Consumer<String> output, Consumer<Integer> done) throws IOException {
        if (process != null && process.isAlive()) throw new IOException("A program is already running");
        Path binary = home.resolve(windows() ? "luac.exe" : "luac");
        Path gcc = home.resolve(windows() ? "gcc/bin/gcc.exe" : "gcc/bin/gcc");
        Path tcc = home.resolve(windows() ? "tcc/tcc.exe" : "tcc/bin/tcc");
        Path bundledCompiler = Files.isRegularFile(gcc) ? gcc : tcc;
        if (!Files.isRegularFile(binary) || !Files.isRegularFile(bundledCompiler)) throw new IOException("Bundled LuaC or C compiler is missing: " + home);
        List<String> args = new ArrayList<>(Arrays.asList(binary.toString(), "--cc", bundledCompiler.toString(), mode, source.toString()));
        ProcessBuilder builder = new ProcessBuilder(args).directory(project.toFile()).redirectErrorStream(true);
        // A user's compiler flags or PATH must not silently replace the bundled toolchain.
        builder.environment().remove("CC"); builder.environment().remove("LUAC_CFLAGS"); builder.environment().remove("LUAC_LDFLAGS");
        builder.environment().put("LUAC_RUNTIME", home.resolve("runtime.h").toString().replace('\\', '/'));
        String path = builder.environment().getOrDefault("PATH", "");
        builder.environment().put("PATH", bundledCompiler.getParent() + File.pathSeparator + home + File.pathSeparator + path);
        Process started = builder.start(); process = started;
        Thread reader = new Thread(() -> {
            int result = -1;
            try (Reader stream = new InputStreamReader(started.getInputStream(), StandardCharsets.UTF_8)) {
                char[] chunk = new char[2048]; int n;
                while ((n = stream.read(chunk)) != -1) output.accept(new String(chunk, 0, n));
                result = started.waitFor();
            } catch (IOException e) { output.accept("\n" + e.getMessage() + "\n"); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            synchronized (Compiler.this) { if (process == started) process = null; }
            done.accept(result);
        }, "luac-output");
        reader.setDaemon(true); reader.start();
    }
    synchronized void stop() {
        if (process != null) {
            process.descendants().forEach(ProcessHandle::destroyForcibly);
            process.destroyForcibly();
        }
    }
}
