package com.luac.studio;

import static org.junit.Assert.*;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class SceneProjectTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    @Test public void sceneRoundTripsInArchiveAndNeverRewritesScripts() throws Exception {
        ProjectStore store = new ProjectStore(temporary.newFolder("projects"));
        File project = store.createSceneProject("Scene");
        SceneProject scene = SceneProject.open(store, project);
        String id = scene.addActor("Кот \"Мяу\"");
        File script = scene.script(id);
        String lua = "print('actor script')\n";
        store.write(script, lua);
        store.write(new File(project, "assets/look.png"), "image");
        scene.setLook(id, "assets/look.png");
        scene.setLook("background", "assets/look.png");
        String generated = store.read(new File(project, "scene.lua"));
        assertTrue(generated.contains("engine.create(world, \"Кот \\\"Мяу\\\"\", 0, 0)"));
        assertTrue(generated.contains("require('actors." + id + "')"));
        assertTrue(generated.contains("engine.set_sprite(sprite"));
        assertTrue(generated.contains("background.z = -1"));
        // Earlier generated scenes are upgraded only when their body is unchanged.
        File sceneFile = new File(project, "scene.lua");
        store.write(sceneFile, generated.replace("background.z = -1\n", ""));
        SceneProject.open(store, project);
        assertEquals(generated, store.read(sceneFile));
        assertEquals(lua, store.read(script));
        ByteArrayOutputStream archive = new ByteArrayOutputStream();
        store.exportProject(project, archive);
        File imported = store.importProject(new ByteArrayInputStream(archive.toByteArray()));
        SceneProject restored = SceneProject.open(store, imported);
        assertEquals("Кот \"Мяу\"", restored.actor(id).getString("name"));
        assertEquals("assets/look.png", restored.background());
        assertEquals(lua, store.read(restored.script(id)));
        assertEquals(generated, store.read(new File(imported, "scene.lua")));
    }

    @Test public void editsAreNotOverwrittenAndInvalidImagesAreRejected() throws Exception {
        ProjectStore store = new ProjectStore(temporary.newFolder("projects"));
        File project = store.createSceneProject("Scene");
        SceneProject first = SceneProject.open(store, project);
        SceneProject stale = SceneProject.open(store, project);
        String id = first.addActor("Hero");
        try { stale.addActor("Stale"); fail("stale save"); } catch (IOException expected) { }
        assertEquals(1, SceneProject.open(store, project).actors().length());
        assertEquals(2, store.children(new File(project, "actors")).size());
        for (String path : new String[] {"../secret.png", "assets/../bad.png", "assets/missing.png", "assets/a.txt"}) {
            try { first.setLook(id, path); fail("bad look " + path); } catch (IOException expected) { }
        }
        File sceneFile = new File(project, "scene.lua");
        String changed = store.read(sceneFile) + "print('manual edit')\n";
        Files.write(sceneFile.toPath(), changed.getBytes(StandardCharsets.UTF_8));
        try { SceneProject.open(store, project); fail("modified source"); } catch (IOException expected) { }
        assertEquals(changed, store.read(sceneFile));
        try { first.addActor("Another"); fail("stale source"); } catch (IOException expected) { }
        assertEquals(changed, store.read(sceneFile));
    }
}
