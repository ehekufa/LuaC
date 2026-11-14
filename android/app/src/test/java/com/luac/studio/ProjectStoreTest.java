package com.luac.studio;

import static org.junit.Assert.*;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ProjectStoreTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    @Test public void renamesFilesFoldersProjectsAndPreservesContents() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        File project = new File(root, "demo");
        File folder = new File(project, "scripts");
        assertTrue(folder.mkdirs());
        File file = new File(folder, "old.lua");
        Files.write(file.toPath(), "print('ok')".getBytes(StandardCharsets.UTF_8));
        File renamed = store.rename(file, "new.lua");
        assertFalse(file.exists());
        assertEquals("print('ok')", store.read(renamed));
        folder = store.rename(folder, "src");
        project = store.rename(project, "game");
        assertTrue(new File(project, "src/new.lua").isFile());
        // Renaming main.lua must not hide a project from the list.
        assertTrue(store.projects().contains(project));
    }

    @Test public void paintingSavesInAssetsAndNeverOverwrites() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        File project = new File(root, "game");
        assertTrue(project.mkdir());
        byte[] png = {(byte) 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
        File saved = store.savePainting(project, "sprite", output -> output.write(png));
        assertEquals(new File(project, "assets/sprite.png"), saved);
        assertArrayEquals(png, Files.readAllBytes(saved.toPath()));
        try { store.savePainting(project, "sprite.png", output -> output.write(0)); fail("overwrite"); }
        catch (IOException expected) { assertArrayEquals(png, Files.readAllBytes(saved.toPath())); }
        for (String name : new String[] {"", "../bad", "x/y", "x\\y", ".."}) {
            try { store.savePainting(project, name, output -> {}); fail("unsafe: " + name); }
            catch (IOException expected) { }
        }
        try { store.savePainting(project, "incomplete", output -> { output.write(1); throw new IOException("disk error"); }); fail("failure"); }
        catch (IOException expected) { assertFalse(new File(project, "assets/incomplete.png").exists()); }
        File outside = temporary.newFolder("outside");
        try { store.savePainting(outside, "bad", output -> {}); fail("outside"); }
        catch (IOException expected) { }
    }

    @Test public void refusesCollisionsInvalidNamesAndOutsideFiles() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        File a = new File(root, "a.lua"), b = new File(root, "b.lua");
        assertTrue(a.createNewFile());
        assertTrue(b.createNewFile());
        for (String name : new String[] {"b.lua", "", "..", "x/y", "x\\y"}) {
            try { store.rename(a, name); fail("Accepted " + name); }
            catch (IOException expected) { assertTrue(a.exists()); }
        }
        File outside = temporary.newFile("outside.lua");
        try { store.rename(outside, "else.lua"); fail("Accepted outside file"); }
        catch (IOException expected) { assertTrue(outside.exists()); }
        assertEquals(a, store.rename(a, "a.lua"));
    }
}
