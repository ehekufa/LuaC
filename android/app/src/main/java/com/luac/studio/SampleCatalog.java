package com.luac.studio;

import android.content.res.AssetManager;
import android.content.Context;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;

/** Curated, local sample projects. Installing always makes an editable copy. */
final class SampleCatalog {
    static final class Sample {
        final String id, name, description, module;
        Sample(String id, String name, String description, String module) {
            this.id = id; this.name = name; this.description = description; this.module = module;
        }
    }
    static final Sample[] SAMPLES = {
        new Sample("hello", "Hello, modules", "A main file and a project module", "greeting.lua"),
        new Sample("library", "Built-in libraries", "JSON, SHA-256 and Base64", "data.lua"),
        new Sample("gui", "luac_gui • touch", "A small Canvas scene wrapper, not Vulkan", "luac_gui.lua"),
        new Sample("love", "LÖVE-style drawing", "Draw and touch callbacks on Canvas", "scene.lua"),
        new Sample("ffi", "Native FFI", "C library calls; requires a compatible libffi", "native.lua")
    };
    static String description(Context context, Sample sample) {
        if (!StudioTheme.language(context).equals("ru")) return sample.description;
        switch (sample.id) {
            case "hello": return "Главный файл и модуль проекта";
            case "library": return "JSON, SHA-256 и Base64";
            case "gui": return "Сцена Canvas с касанием, без Vulkan";
            case "love": return "Рисование и касания на Canvas";
            case "ffi": return "Вызовы C-библиотеки; требуется совместимая libffi";
            default: return sample.description;
        }
    }
    private SampleCatalog() { }

    static File install(AssetManager assets, ProjectStore store, Sample sample) throws IOException {
        ProjectArchive.Document document = new ProjectArchive.Document(sample.name);
        document.directories.add("scripts");
        document.directories.add("assets");
        String[] paths = sample.id.equals("love")
            ? new String[] {"main.lua", "scripts/" + sample.module, "assets/luac_logo.png"}
            : new String[] {"main.lua", "scripts/" + sample.module};
        for (String path : paths) {
            try (InputStream in = assets.open("samples/" + sample.id + "/" + path);
                 ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[4096];
                int count;
                while ((count = in.read(buffer)) != -1) {
                    if (out.size() + count > 1024 * 1024) throw new IOException("Sample file is too large");
                    out.write(buffer, 0, count);
                }
                document.files.add(new ProjectArchive.Entry(path, out.toByteArray()));
            }
        }
        return store.installDocument(document);
    }
}
