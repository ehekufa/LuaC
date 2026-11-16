package com.luac.studio;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Base64;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** A scene is an explicitly created project, not a conversion of arbitrary Lua. */
final class SceneProject {
    private static final String HEADER = "-- LuaC-scene-v1:";
    static final String MARKER = ".luac-scene";
    private final ProjectStore store;
    private final File project;
    private final JSONObject data;
    private String loaded;

    private SceneProject(ProjectStore store, File project, JSONObject data, String loaded) {
        this.store = store; this.project = project; this.data = data; this.loaded = loaded;
    }

    static boolean isScene(File project) { return new File(project, MARKER).isFile(); }

    static void create(ProjectStore store, File project) throws IOException {
        try {
            if (!new File(project, "actors").mkdirs() || !new File(project, "assets").mkdirs())
                throw new IOException("Cannot create scene folders");
            store.write(new File(project, "actors/background.lua"), "-- Background script\n");
            store.write(new File(project, "main.lua"), "-- Scene entry point\nrequire('scene')\n");
            JSONObject data = new JSONObject().put("version", 1).put("background", "").put("actors", new JSONArray());
            SceneProject scene = new SceneProject(store, project, data, null);
            store.write(new File(project, "scene.lua"), scene.render());
            store.write(new File(project, MARKER), "LuaC scene project, version 1\n");
        } catch (JSONException error) { throw new IOException("Cannot create scene", error); }
    }

    static SceneProject open(ProjectStore store, File project) throws IOException {
        if (!isScene(project)) throw new IOException("Not a scene project");
        String source = store.read(new File(project, "scene.lua"));
        try {
            int end = source.indexOf('\n');
            if (!source.startsWith(HEADER) || end < 0) throw new IOException("Scene metadata is missing");
            String json = new String(Base64.getDecoder().decode(source.substring(HEADER.length(), end)), StandardCharsets.UTF_8);
            JSONObject data = new JSONObject(json);
            SceneProject scene = new SceneProject(store, project, data, source);
            scene.validate();
            if (!(source.substring(0, end + 1) + scene.body()).equals(source))
                throw new IOException("scene.lua was edited outside the scene editor; it was not overwritten");
            return scene;
        } catch (JSONException | IllegalArgumentException error) { throw new IOException("Invalid scene metadata", error); }
    }

    JSONArray actors() { return data.optJSONArray("actors"); }
    String background() { return data.optString("background", ""); }
    File script(String id) throws IOException {
        if (!id.equals("background") && !id.matches("a[0-9a-f]{32}")) throw new IOException("Invalid actor ID");
        return store.resolve(project, "actors/" + id + ".lua");
    }
    JSONObject actor(String id) throws IOException {
        JSONArray actors = actors();
        for (int i = 0; i < actors.length(); i++) {
            JSONObject actor = actors.optJSONObject(i);
            if (id.equals(actor.optString("id"))) return actor;
        }
        throw new IOException("Actor not found");
    }
    String addActor(String name) throws IOException {
        name = name == null ? "" : name.trim();
        if (name.isEmpty() || name.length() > 100) throw new IOException("Actor name must contain 1–100 characters");
        if (actors().length() >= 100) throw new IOException("Scene actor limit reached");
        String id = "a" + UUID.randomUUID().toString().replace("-", "");
        File file = script(id);
        if (!file.createNewFile()) throw new IOException("Actor script already exists");
        try {
            actors().put(new JSONObject().put("id", id).put("name", name).put("look", ""));
            save();
        } catch (Exception error) {
            actors().remove(actors().length() - 1);
            if (!file.delete()) error.addSuppressed(new IOException("Could not remove incomplete actor script"));
            throw new IOException("Cannot add actor", error);
        }
        return id;
    }
    void setLook(String id, String path) throws IOException {
        validateLook(path);
        if (!path.isEmpty() && !store.resolve(project, path).isFile()) throw new IOException("Image not found");
        try {
            JSONObject target = id.equals("background") ? data : actor(id);
            String key = id.equals("background") ? "background" : "look";
            String previous = target.getString(key);
            target.put(key, path);
            try { save(); } catch (IOException error) { target.put(key, previous); throw error; }
        } catch (JSONException error) { throw new IOException("Cannot update look", error); }
    }
    private void validate() throws JSONException, IOException {
        if (data.getInt("version") != 1) throw new IOException("Unsupported scene version");
        validateLook(data.getString("background"));
        JSONArray actors = data.getJSONArray("actors");
        if (actors.length() > 100) throw new IOException("Scene actor limit reached");
        Set<String> ids = new HashSet<>();
        for (int i = 0; i < actors.length(); i++) {
            JSONObject actor = actors.getJSONObject(i);
            String id = actor.getString("id"), name = actor.getString("name");
            if (!id.matches("a[0-9a-f]{32}") || !ids.add(id) || name.trim().isEmpty() || name.length() > 100)
                throw new IOException("Invalid actor");
            validateLook(actor.getString("look"));
        }
    }
    private static void validateLook(String path) throws IOException {
        if (path.isEmpty()) return;
        String lower = path.toLowerCase(Locale.ROOT);
        if (!path.startsWith("assets/") || path.contains("\\") || path.contains("..") ||
            !(lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")))
            throw new IOException("Invalid image path");
    }
    private String render() throws JSONException, IOException {
        validate();
        String encoded = Base64.getEncoder().encodeToString(data.toString().getBytes(StandardCharsets.UTF_8));
        return HEADER + encoded + "\n" + body();
    }
    private String body() throws JSONException {
        StringBuilder out = new StringBuilder("-- Generated by LuaC scene editor. Edit object scripts instead.\nworld = engine.world()\n");
        if (!background().isEmpty()) {
            out.append("local background = engine.create(world, 'Background', 0, 0)\n");
            out.append("engine.set_sprite(background, assert(engine.load_texture(").append(BlockProgram.quote(background())).append(")), 800, 600)\n");
        }
        out.append("require('actors.background')\n");
        JSONArray actors = actors();
        for (int i = 0; i < actors.length(); i++) {
            JSONObject actor = actors.getJSONObject(i);
            out.append("sprite = engine.create(world, ").append(BlockProgram.quote(actor.getString("name"))).append(", 0, 0)\n");
            String look = actor.getString("look");
            if (!look.isEmpty()) out.append("engine.set_sprite(sprite, assert(engine.load_texture(")
                .append(BlockProgram.quote(look)).append(")), 64, 64)\n");
            out.append("require('actors.").append(actor.getString("id")).append("')\n");
        }
        return out.toString();
    }
    private void save() throws IOException {
        File target = new File(project, "scene.lua");
        if (!store.read(target).equals(loaded)) throw new IOException("Scene changed on disk; reopen it before editing");
        File temp = File.createTempFile(".scene-", ".tmp", project);
        try {
            String next = render();
            store.write(temp, next);
            try { Files.move(temp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE); }
            catch (AtomicMoveNotSupportedException error) { Files.move(temp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING); }
            loaded = next;
        } catch (JSONException error) { throw new IOException("Cannot save scene", error); }
        finally { Files.deleteIfExists(temp.toPath()); }
    }
}
