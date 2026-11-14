package com.luac.studio;

import static org.junit.Assert.*;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import org.json.JSONObject;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ProjectArchiveTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    @Test public void roundTripKeepsFilesImagesEmptyFoldersAndAvoidsOverwrite() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        File project = new File(root, "Game");
        assertTrue(new File(project, "scripts/empty").mkdirs());
        assertTrue(new File(project, "assets").mkdirs());
        byte[] source = "print('Привет')\n".getBytes(StandardCharsets.UTF_8);
        byte[] image = {(byte)0x89, 0x50, 0x4e, 0x47, 0, (byte)0xff};
        Files.write(new File(project, "main.lua").toPath(), source);
        Files.write(new File(project, "assets/sprite.png").toPath(), image);
        Files.write(new File(project, "scripts/blank.lua").toPath(), new byte[0]);

        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        store.exportProject(project, exported);
        JSONObject json = new JSONObject(exported.toString("UTF-8"));
        assertEquals("luac-project", json.getString("format"));
        assertEquals(1, json.getInt("version"));
        File imported = store.importProject(new ByteArrayInputStream(exported.toByteArray()));
        assertEquals("Game 2", imported.getName());
        assertArrayEquals(source, Files.readAllBytes(new File(imported, "main.lua").toPath()));
        assertArrayEquals(image, Files.readAllBytes(new File(imported, "assets/sprite.png").toPath()));
        assertEquals(0, new File(imported, "scripts/blank.lua").length());
        assertTrue(new File(imported, "scripts/empty").isDirectory());
        assertArrayEquals(source, Files.readAllBytes(new File(project, "main.lua").toPath()));
    }

    @Test public void rejectsUnsafeOrInvalidArchivesBeforeCreatingAProject() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        String[] bad = {
            "not json", "{}", archive("../outside", ""),
            archive("/absolute", ""), archive("scripts/../bad", ""),
            archive("scripts\\\\evil", ""), archive("C:bad", ""),
            archive("okay", "not base64!!!"),
            "{\"format\":\"luac-project\",\"version\":2,\"name\":\"x\",\"directories\":[],\"files\":[]}",
            "{\"format\":\"luac-project\",\"version\":1,\"name\":\"x\",\"directories\":[\"a\"],\"files\":[{\"path\":\"a\",\"data\":\"\"}]}",
            "{\"format\":\"luac-project\",\"version\":1,\"name\":\"x\",\"directories\":[\"a/b\"],\"files\":[{\"path\":\"a\",\"data\":\"\"}]}",
            "{\"format\":\"luac-project\",\"version\":1,\"name\":\"x\",\"directories\":[],\"files\":[]} trailing"
        };
        for (String text : bad) {
            try { store.importProject(new ByteArrayInputStream(text.getBytes(StandardCharsets.UTF_8))); fail(text); }
            catch (IOException expected) { assertEquals(0, root.list().length); }
        }
        try { store.importProject(new ByteArrayInputStream(new byte[] {(byte)0xc3, 0x28})); fail("invalid UTF-8"); }
        catch (IOException expected) { assertEquals(0, root.list().length); }
    }

    @Test public void failedInstallLeavesNoProjectOrStagingDirectory() throws Exception {
        File parent = temporary.newFolder("parent");
        File root = new File(parent, "projects");
        ProjectStore store = new ProjectStore(root);
        String tooLong = new String(new char[300]).replace('\0', 'a');
        String archive = archive(tooLong, "");
        try {
            store.importProject(new ByteArrayInputStream(archive.getBytes(StandardCharsets.UTF_8)));
            fail("filesystem must reject an overly long filename");
        } catch (IOException expected) {
            assertEquals(0, root.list().length);
            assertArrayEquals(new String[] {"projects"}, parent.list());
        }
    }

    @Test public void refusesOversizedFilesAndLinksOnExport() throws Exception {
        File root = temporary.newFolder("projects");
        ProjectStore store = new ProjectStore(root);
        File project = new File(root, "Big");
        assertTrue(project.mkdir());
        try (java.io.RandomAccessFile file = new java.io.RandomAccessFile(new File(project, "huge.bin"), "rw")) {
            file.setLength(8L * 1024 * 1024 + 1);
        }
        try { store.exportProject(project, new ByteArrayOutputStream()); fail("oversized file"); }
        catch (IOException expected) { }
        assertTrue(new File(project, "huge.bin").delete());
        File outside = temporary.newFile("outside");
        Files.createSymbolicLink(new File(project, "link").toPath(), outside.toPath());
        try { store.exportProject(project, new ByteArrayOutputStream()); fail("symlink"); }
        catch (IOException expected) { }
    }

    private static String archive(String path, String data) {
        return "{\"format\":\"luac-project\",\"version\":1,\"name\":\"x\",\"directories\":[],\"files\":[{\"path\":\"" + path + "\",\"data\":\"" + data + "\"}]}";
    }
}
