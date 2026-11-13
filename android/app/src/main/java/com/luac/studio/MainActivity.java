package com.luac.studio;

import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.floatingactionbutton.ExtendedFloatingActionButton;
import com.google.android.material.textfield.TextInputEditText;
import java.io.File;
import java.io.InputStream;
import org.json.JSONObject;

public final class MainActivity extends AppCompatActivity {
    private static final int PICK_ASSET = 41;

    private static boolean nativeAvailable;
    private static String nativeLoadError = "";
    static {
        try { System.loadLibrary("luac_native"); nativeAvailable = true; }
        catch (Throwable error) { nativeLoadError = error.getClass().getSimpleName(); }
    }

    private static native String nativeCheck(String source);
    private static native String nativeGenerate(String source);
    private static native String nativeVersion();
    private static native String nativeOpenProject(String root);
    private static native String nativeStepProject(double dt);
    private static native void nativeTouchProject(int id, double x, double y, boolean down);
    private static native void nativeCloseProject();
    private static native boolean nativeVulkanCreate(android.view.Surface surface);
    private static native void nativeVulkanDraw();
    private static native void nativeVulkanDestroy();

    private final Handler handler = new Handler(Looper.getMainLooper());
    private ProjectStore store;
    private File project, directory, editorFile, imageFile, importDirectory;
    private EditText editor;
    private TextView output;
    private RuntimeView runtimeView;
    private boolean running, opening, runtimeError, destroyed;
    private long previousFrame;
    private final StringBuilder console = new StringBuilder();

    private final Runnable frame = new Runnable() {
        @Override public void run() {
            if (!running || !nativeAvailable) return;
            long now = System.nanoTime();
            double dt = previousFrame == 0 ? 1.0 / 60 : Math.min(.05, (now - previousFrame) / 1e9);
            previousFrame = now;
            try { applyFrame(new JSONObject(nativeStepProject(dt))); }
            catch (Throwable error) { appendConsole("Runtime bridge error: " + error + "\n"); running = false; }
            if (running) handler.postDelayed(this, 16);
        }
    };

    @Override public void onCreate(Bundle state) {
        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES);
        super.onCreate(state);
        store = new ProjectStore(this);
        try { if (store.projects().isEmpty()) store.createProject("Demo"); }
        catch (Exception error) { toast(error); }
        showProjects();
    }

    private void toast(Throwable error) { Toast.makeText(this, String.valueOf(error.getMessage() == null ? error : error.getMessage()), Toast.LENGTH_LONG).show(); }
    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }

    private MaterialToolbar toolbar(int layout) {
        setContentView(layout);
        MaterialToolbar bar = findViewById(R.id.toolbar);
        bar.setTitleTextColor(Color.WHITE);
        bar.setSubtitleTextColor(Color.WHITE);
        return bar;
    }
    private void back(MaterialToolbar bar, Runnable action) {
        bar.setNavigationIcon(R.drawable.ic_back);
        bar.setNavigationOnClickListener(v -> action.run());
    }
    private String relative(File file) {
        return project.toURI().relativize(file.toURI()).getPath();
    }

    private void showProjects() {
        stopRuntime();
        project = directory = editorFile = imageFile = null;
        MaterialToolbar bar = toolbar(R.layout.screen_projects);
        String version = "native unavailable";
        if (nativeAvailable) try { version = nativeVersion(); } catch (Throwable ignored) { }
        bar.setTitle("LuaC Studio");
        bar.setSubtitle(version + " • in-process runtime");
        RecyclerView list = findViewById(R.id.list);
        list.setLayoutManager(new LinearLayoutManager(this));
        ProjectAdapter adapter = new ProjectAdapter(store, store.projects(), new ProjectAdapter.Listener() {
            @Override public void onOpen(File item) { showProject(item); }
            @Override public void onRun(File item) { startProject(item); }
            @Override public void onRename(File item) {
                renameDialog(item, renamed -> { showProjects(); toast("Renamed to " + renamed.getName()); });
            }
            @Override public void onDelete(File item) { confirmDelete(item, MainActivity.this::showProjects); }
        });
        list.setAdapter(adapter);
        ExtendedFloatingActionButton fab = findViewById(R.id.fab);
        fab.setOnClickListener(v -> newProjectDialog());
    }

    private void newProjectDialog() {
        nameDialog("New project", "", "Project name", "Create", name -> {
            try { showProject(store.createProject(name)); }
            catch (Exception error) { toast(error); }
        });
    }

    private void showProject(File selected) { project = selected; showDirectory(selected); }

    private void showDirectory(File selected) {
        stopRuntime();
        directory = selected;
        editorFile = imageFile = null;
        MaterialToolbar bar = toolbar(R.layout.screen_files);
        bar.setTitle(project.getName());
        String path = relative(selected);
        bar.setSubtitle(path.isEmpty() ? "Project files" : path);
        back(bar, this::onBackPressed);
        bar.inflateMenu(R.menu.menu_files);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_run) startProject(project);
            else if (item.getItemId() == R.id.action_add_file) nameDialog("New file", "", "script.lua", "Create", name -> {
                try { showEditor(store.createFile(directory, name)); } catch (Exception error) { toast(error); }
            });
            else if (item.getItemId() == R.id.action_add_folder) nameDialog("New folder", "", "scripts", "Create", name -> {
                try { store.createFolder(directory, name); showDirectory(directory); } catch (Exception error) { toast(error); }
            });
            else if (item.getItemId() == R.id.action_import) pickAsset();
            return true;
        });
        RecyclerView list = findViewById(R.id.list);
        list.setLayoutManager(new LinearLayoutManager(this));
        FileAdapter adapter = new FileAdapter(store.children(directory), new FileAdapter.Listener() {
            @Override public void onOpen(File file) { openFile(file); }
            @Override public void onRename(File file) {
                renameDialog(file, renamed -> { showDirectory(directory); toast("Renamed to " + renamed.getName()); });
            }
            @Override public void onDelete(File file) { confirmDelete(file, () -> showDirectory(directory)); }
        });
        list.setAdapter(adapter);
    }

    private void openFile(File file) {
        if (file.isDirectory()) showDirectory(file);
        else if (isImage(file)) showImage(file);
        else showEditor(file);
    }
    private static boolean isImage(File file) {
        String name = file.getName().toLowerCase();
        return name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".webp");
    }

    private void showImage(File file) {
        imageFile = file;
        directory = file.getParentFile();
        MaterialToolbar bar = toolbar(R.layout.screen_image);
        bar.setTitle(file.getName());
        bar.setSubtitle(relative(file));
        back(bar, () -> showDirectory(file.getParentFile()));
        bar.inflateMenu(R.menu.menu_image);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_rename) renameDialog(file, renamed -> showImage(renamed));
            else if (item.getItemId() == R.id.action_delete) confirmDelete(file, () -> showDirectory(file.getParentFile()));
            return true;
        });
        ImageView image = findViewById(R.id.image);
        image.setImageURI(Uri.fromFile(file));
        TextView path = findViewById(R.id.path);
        path.setText("Use it with engine.load_texture('" + relative(file) + "').");
    }

    private void showEditor(File file) {
        stopRuntime();
        editorFile = file;
        imageFile = null;
        directory = file.getParentFile();
        MaterialToolbar bar = toolbar(R.layout.screen_editor);
        bar.setTitle(file.getName());
        bar.setSubtitle(relative(file));
        back(bar, () -> { if (saveEditor()) showDirectory(directory); });
        bar.inflateMenu(R.menu.menu_editor);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_rename) renameEditorFile();
            else if (item.getItemId() == R.id.action_check) { if (saveEditor()) output.setText(compilerCall(editor.getText().toString(), false)); }
            else if (item.getItemId() == R.id.action_generate) { if (saveEditor()) output.setText(compilerCall(editor.getText().toString(), true)); }
            else if (item.getItemId() == R.id.action_delete) confirmDelete(editorFile, () -> showDirectory(directory));
            return true;
        });
        editor = findViewById(R.id.editor);
        try { editor.setText(store.read(file)); } catch (Exception error) { toast(error); showDirectory(directory); return; }
        MaterialButton save = findViewById(R.id.save);
        save.setOnClickListener(v -> { if (saveEditor()) toast("Saved"); });
        MaterialButton run = findViewById(R.id.run);
        run.setOnClickListener(v -> { if (saveEditor()) startProject(project); });
        output = findViewById(R.id.output);
    }

    private void renameEditorFile() {
        if (!saveEditor()) return;
        File current = editorFile;
        renameDialog(current, renamed -> {
            editorFile = renamed;
            directory = renamed.getParentFile();
            MaterialToolbar bar = findViewById(R.id.toolbar);
            bar.setTitle(renamed.getName());
            bar.setSubtitle(relative(renamed));
            toast("Renamed to " + renamed.getName());
        });
    }

    private boolean saveEditor() {
        if (editor == null || editorFile == null) return true;
        try { store.write(editorFile, editor.getText().toString()); return true; }
        catch (Exception error) { toast(error); return false; }
    }
    private String compilerCall(String source, boolean generate) {
        if (!nativeAvailable) return "Native library unavailable: " + nativeLoadError;
        try { return generate ? nativeGenerate(source) : nativeCheck(source); }
        catch (Throwable error) { return "Compiler error: " + error; }
    }

    private void startProject(File selected) {
        if (opening) return;
        stopRuntime();
        project = selected;
        if (!new File(selected, "main.lua").isFile()) { toast("main.lua is missing"); return; }
        if (!nativeAvailable) { toast("Native library unavailable: " + nativeLoadError); return; }
        opening = true;
        MaterialToolbar bar = toolbar(R.layout.screen_runtime);
        bar.setTitle("Starting " + selected.getName() + "…");
        bar.setSubtitle("Loading main.lua, modules and textures");
        new Thread(() -> {
            String result;
            try { result = nativeOpenProject(selected.getAbsolutePath()); }
            catch (Throwable error) { result = String.valueOf(error); }
            final String completed = result;
            handler.post(() -> {
                opening = false;
                if (destroyed) { try { nativeCloseProject(); } catch (Throwable ignored) { } return; }
                try { showRuntime(selected, new JSONObject(completed)); }
                catch (Exception error) {
                    new MaterialAlertDialogBuilder(this).setTitle("Project could not start").setMessage(completed)
                        .setPositiveButton("OK", (dialog, which) -> showProject(selected)).show();
                }
            });
        }, "LuaC-project-loader").start();
    }

    private void showRuntime(File selected, JSONObject initial) {
        project = selected;
        console.setLength(0);
        runtimeError = false;
        MaterialToolbar bar = toolbar(R.layout.screen_runtime);
        bar.setTitle(selected.getName());
        bar.setSubtitle("Running • touch the scene");
        bar.inflateMenu(R.menu.menu_runtime);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_stop) showProject(selected);
            return true;
        });
        runtimeView = new RuntimeView(this, selected, (id, x, y, down) -> {
            if (running) nativeTouchProject(id, x, y, down);
        });
        FrameLayout game = findViewById(R.id.game);
        game.addView(runtimeView, new FrameLayout.LayoutParams(-1, -1));
        output = findViewById(R.id.output);
        running = true;
        previousFrame = 0;
        applyFrame(initial);
        if (running) handler.post(frame);
    }

    private void applyFrame(JSONObject value) {
        if (runtimeView == null) return;
        runtimeView.setFrame(value);
        appendConsole(value.optString("output"));
        if (!value.optBoolean("ok", false) && !runtimeError) {
            runtimeError = true;
            running = false;
            appendConsole("\n" + value.optString("error", "Runtime stopped") + "\n");
        }
    }
    private void appendConsole(String message) {
        if (message == null || message.isEmpty() || output == null) return;
        console.append(message);
        if (console.length() > 100000) console.delete(0, console.length() - 80000);
        output.setText(console.toString());
    }

    private void stopRuntime() {
        handler.removeCallbacks(frame);
        boolean wasRunning = running;
        running = false;
        previousFrame = 0;
        if (nativeAvailable && (wasRunning || runtimeView != null)) {
            try { nativeCloseProject(); } catch (Throwable ignored) { }
        }
        if (runtimeView != null) runtimeView.dispose();
        runtimeView = null;
    }

    private void pickAsset() {
        importDirectory = directory;
        String path = relative(directory);
        if (!path.startsWith("assets/")) importDirectory = new File(project, "assets");
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        startActivityForResult(intent, PICK_ASSET);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ASSET || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            File imported = store.importFile(importDirectory, displayName(uri), input);
            toast("Imported " + imported.getName());
            showDirectory(importDirectory);
        } catch (Exception error) { toast(error); }
    }
    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) return cursor.getString(column);
            }
        }
        return "asset.png";
    }

    private void nameDialog(String title, String initial, String hint, String positive, NameAction action) {
        View view = LayoutInflater.from(this).inflate(R.layout.dialog_name, null);
        TextInputEditText input = view.findViewById(R.id.input);
        input.setText(initial);
        input.setHint(hint);
        if (!initial.isEmpty()) { input.setSelection(initial.length()); }
        new MaterialAlertDialogBuilder(this).setTitle(title).setView(view)
            .setPositiveButton(positive, (dialog, which) -> action.run(input.getText().toString()))
            .setNegativeButton("Cancel", null).show();
    }
    private void renameDialog(File file, RenameAction after) {
        String title = file.getName().equals("main.lua") ? "Rename entry point (main.lua is required to run)" : "Rename (references are not updated)";
        nameDialog(title, file.getName(), file.getName(), "Rename", name -> {
            try {
                File renamed = store.rename(file, name);
                if (renamed.equals(file)) { toast("Name unchanged"); return; }
                after.run(renamed);
            } catch (Exception error) { toast(error); }
        });
    }
    private interface NameAction { void run(String name); }
    private interface RenameAction { void run(File renamed); }

    private void confirmDelete(File file, Runnable after) {
        new MaterialAlertDialogBuilder(this).setTitle("Delete " + file.getName() + "?")
            .setMessage(file.isDirectory() ? "The folder and everything inside it will be deleted." : "This cannot be undone.")
            .setPositiveButton("Delete", (dialog, which) -> {
                try { store.delete(file); after.run(); } catch (Exception error) { toast(error); }
            }).setNegativeButton("Cancel", null).show();
    }

    @Override public void onBackPressed() {
        if (opening) return;
        if (running || runtimeView != null) { showProject(project); return; }
        if (editorFile != null) { if (saveEditor()) showDirectory(directory); return; }
        if (imageFile != null) { showDirectory(imageFile.getParentFile()); return; }
        if (project != null) {
            if (directory != null && !directory.equals(project)) showDirectory(directory.getParentFile());
            else showProjects();
            return;
        }
        super.onBackPressed();
    }
    @Override protected void onPause() {
        if (editorFile != null) saveEditor();
        super.onPause();
    }
    @Override protected void onDestroy() {
        destroyed = true;
        stopRuntime();
        if (nativeAvailable) try { nativeVulkanDestroy(); } catch (Throwable ignored) { }
        super.onDestroy();
    }
}
