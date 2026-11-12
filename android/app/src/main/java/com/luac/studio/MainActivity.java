package com.luac.studio;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.InputStream;
import org.json.JSONObject;

public final class MainActivity extends Activity {
    private static final int PICK_ASSET = 41;
    private static final int BACKGROUND = Color.rgb(9, 13, 22);
    private static final int PANEL = Color.rgb(14, 21, 34);
    private static final int ACCENT = Color.rgb(66, 221, 176);
    private static final int TEXT = Color.rgb(205, 220, 231);

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
    private File project, directory, editorFile, importDirectory;
    private EditText editor;
    private TextView output;
    private RuntimeView runtimeView;
    private boolean running, opening, runtimeError;
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
        super.onCreate(state);
        store = new ProjectStore(this);
        try { if (store.projects().isEmpty()) store.createProject("Demo"); }
        catch (Exception error) { toast(error); }
        showProjects();
    }

    private LinearLayout screen() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BACKGROUND);
        root.setPadding(18, 12, 18, 12);
        setContentView(root);
        return root;
    }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(4, 8, 4, 8);
        return view;
    }
    private Button button(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label); button.setOnClickListener(listener);
        return button;
    }
    private LinearLayout row() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        return row;
    }
    private void weighted(LinearLayout row, View view) { row.addView(view, new LinearLayout.LayoutParams(0, -2, 1)); }
    private void toast(Throwable error) { Toast.makeText(this, String.valueOf(error.getMessage() == null ? error : error.getMessage()), Toast.LENGTH_LONG).show(); }
    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }

    private void showProjects() {
        stopRuntime();
        project = directory = editorFile = null;
        LinearLayout root = screen();
        String version = "native unavailable";
        if (nativeAvailable) try { version = nativeVersion(); } catch (Throwable ignored) { }
        root.addView(text("LuaC Studio", 28, ACCENT));
        root.addView(text("Projects • " + version + " • in-process runtime", 14, TEXT));
        LinearLayout actions = row();
        weighted(actions, button("＋ NEW PROJECT", v -> newProjectDialog()));
        weighted(actions, button("CREATE DEMO", v -> {
            try { showProject(store.createProject("Demo")); } catch (Exception error) { toast(error); }
        }));
        root.addView(actions);
        ScrollView scroll = new ScrollView(this);
        LinearLayout list = new LinearLayout(this); list.setOrientation(LinearLayout.VERTICAL);
        for (File item : store.projects()) {
            list.addView(text(item.getName(), 21, Color.WHITE));
            LinearLayout buttons = row();
            weighted(buttons, button("FILES", v -> showProject(item)));
            weighted(buttons, button("▶ RUN", v -> startProject(item)));
            weighted(buttons, button("DELETE", v -> confirmDelete(item, this::showProjects)));
            list.addView(buttons);
        }
        if (store.projects().isEmpty()) list.addView(text("Create a project to begin.", 16, TEXT));
        scroll.addView(list); root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        root.addView(text("Each project has main.lua, Lua modules and an assets folder. No PC or C compiler is needed on the phone.", 13, TEXT));
    }

    private void newProjectDialog() {
        EditText name = new EditText(this); name.setHint("Project name"); name.setSingleLine();
        new AlertDialog.Builder(this).setTitle("New project").setView(name)
            .setPositiveButton("Create", (dialog, which) -> {
                try { showProject(store.createProject(name.getText().toString())); }
                catch (Exception error) { toast(error); }
            }).setNegativeButton("Cancel", null).show();
    }

    private void showProject(File selected) { project = selected; showDirectory(selected); }
    private void showDirectory(File selected) {
        stopRuntime(); directory = selected; editorFile = null;
        LinearLayout root = screen();
        root.addView(text(project.getName(), 23, ACCENT));
        String relative = project.toURI().relativize(directory.toURI()).getPath();
        root.addView(text(relative.isEmpty() ? "Project files" : relative, 14, TEXT));
        LinearLayout actions = row();
        weighted(actions, button("▶ RUN", v -> startProject(project)));
        weighted(actions, button("＋FILE", v -> nameDialog("New Lua/text file", "script.lua", name -> {
            try { showEditor(store.createFile(directory, name)); } catch (Exception error) { toast(error); }
        })));
        weighted(actions, button("＋FOLDER", v -> nameDialog("New folder", "scripts", name -> {
            try { store.createFolder(directory, name); showDirectory(directory); } catch (Exception error) { toast(error); }
        })));
        weighted(actions, button("ASSET＋", v -> pickAsset()));
        root.addView(actions);
        if (!directory.equals(project)) root.addView(button("..  PARENT FOLDER", v -> showDirectory(directory.getParentFile())));
        ScrollView scroll = new ScrollView(this);
        LinearLayout list = new LinearLayout(this); list.setOrientation(LinearLayout.VERTICAL);
        for (File item : store.children(directory)) {
            String icon = item.isDirectory() ? "▸  " : isImage(item) ? "▧  " : "   ";
            list.addView(button(icon + item.getName(), v -> openFile(item)));
        }
        scroll.addView(list); root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        root.addView(button("PROJECTS", v -> showProjects()));
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
        LinearLayout root = screen();
        root.addView(text(file.getName(), 22, ACCENT));
        ImageView image = new ImageView(this); image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        image.setImageURI(Uri.fromFile(file)); image.setBackgroundColor(PANEL);
        root.addView(image, new LinearLayout.LayoutParams(-1, 0, 1));
        root.addView(text("Project path: " + project.toURI().relativize(file.toURI()).getPath() + "\nUse it with engine.load_texture().", 15, TEXT));
        LinearLayout actions = row();
        weighted(actions, button("BACK", v -> showDirectory(file.getParentFile())));
        weighted(actions, button("DELETE", v -> confirmDelete(file, () -> showDirectory(file.getParentFile()))));
        root.addView(actions);
    }

    private void showEditor(File file) {
        stopRuntime(); editorFile = file; directory = file.getParentFile();
        LinearLayout root = screen();
        root.addView(text(project.getName() + " / " + project.toURI().relativize(file.toURI()).getPath(), 17, ACCENT));
        editor = new EditText(this);
        try { editor.setText(store.read(file)); } catch (Exception error) { editor.setText(""); toast(error); }
        editor.setTextColor(Color.rgb(220, 230, 241)); editor.setHintTextColor(Color.GRAY);
        editor.setBackgroundColor(PANEL); editor.setPadding(18, 12, 18, 12);
        editor.setGravity(Gravity.TOP); editor.setTypeface(Typeface.MONOSPACE); editor.setTextSize(14);
        editor.setHorizontallyScrolling(true);
        editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        root.addView(editor, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout actions = row();
        weighted(actions, button("SAVE", v -> saveEditor()));
        weighted(actions, button("▶ RUN", v -> { if (saveEditor()) startProject(project); }));
        weighted(actions, button("CHECK", v -> { if (saveEditor()) output.setText(compilerCall(editor.getText().toString(), false)); }));
        weighted(actions, button("C", v -> { if (saveEditor()) output.setText(compilerCall(editor.getText().toString(), true)); }));
        root.addView(actions);
        LinearLayout navigation = row();
        weighted(navigation, button("FILES", v -> { if (saveEditor()) showDirectory(directory); }));
        weighted(navigation, button("DELETE", v -> confirmDelete(file, () -> showDirectory(directory))));
        root.addView(navigation);
        ScrollView scroll = new ScrollView(this); output = text("Ready. RUN executes main.lua and all its require() modules.", 13, TEXT);
        output.setTextIsSelectable(true); scroll.addView(output);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 150));
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
        LinearLayout root = screen();
        root.addView(text("Starting " + selected.getName() + "…", 24, ACCENT));
        root.addView(text("Loading main.lua, modules and textures", 16, TEXT));
        new Thread(() -> {
            String result;
            try { result = nativeOpenProject(selected.getAbsolutePath()); }
            catch (Throwable error) { result = String.valueOf(error); }
            final String completed = result;
            handler.post(() -> {
                opening = false;
                try { showRuntime(selected, new JSONObject(completed)); }
                catch (Exception error) {
                    new AlertDialog.Builder(this).setTitle("Project could not start").setMessage(completed)
                        .setPositiveButton("OK", (dialog, which) -> showProject(selected)).show();
                }
            });
        }, "LuaC-project-loader").start();
    }

    private void showRuntime(File selected, JSONObject initial) {
        project = selected; console.setLength(0); runtimeError = false;
        LinearLayout root = screen();
        LinearLayout title = row();
        weighted(title, text("▶ " + selected.getName(), 19, ACCENT));
        title.addView(button("STOP / EDIT", v -> showProject(selected)));
        root.addView(title);
        runtimeView = new RuntimeView(this, selected, (id, x, y, down) -> {
            if (running) nativeTouchProject(id, x, y, down);
        });
        root.addView(runtimeView, new LinearLayout.LayoutParams(-1, 0, 1));
        ScrollView scroll = new ScrollView(this); output = text("", 13, TEXT); output.setTypeface(Typeface.MONOSPACE);
        output.setTextIsSelectable(true); scroll.addView(output);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 180));
        running = true; previousFrame = 0; applyFrame(initial);
        if (running) handler.post(frame);
    }

    private void applyFrame(JSONObject value) {
        if (runtimeView == null) return;
        runtimeView.setFrame(value);
        appendConsole(value.optString("output"));
        if (!value.optBoolean("ok", false) && !runtimeError) {
            runtimeError = true; running = false;
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
        running = false; previousFrame = 0;
        if (nativeAvailable && (wasRunning || runtimeView != null)) {
            try { nativeCloseProject(); } catch (Throwable ignored) { }
        }
        if (runtimeView != null) runtimeView.dispose();
        runtimeView = null;
    }

    private void pickAsset() {
        importDirectory = directory;
        String relative = project.toURI().relativize(directory.toURI()).getPath();
        if (!relative.startsWith("assets/")) importDirectory = new File(project, "assets");
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("image/*");
        startActivityForResult(intent, PICK_ASSET);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ASSET || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            File imported = store.importFile(importDirectory, displayName(uri), input);
            toast("Imported " + imported.getName()); showDirectory(importDirectory);
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

    private void nameDialog(String title, String hint, NameAction action) {
        EditText input = new EditText(this); input.setHint(hint); input.setSingleLine();
        new AlertDialog.Builder(this).setTitle(title).setView(input)
            .setPositiveButton("Create", (dialog, which) -> action.run(input.getText().toString()))
            .setNegativeButton("Cancel", null).show();
    }
    private interface NameAction { void run(String name); }
    private void confirmDelete(File file, Runnable after) {
        new AlertDialog.Builder(this).setTitle("Delete " + file.getName() + "?")
            .setMessage(file.isDirectory() ? "The folder and everything inside it will be deleted." : "This cannot be undone.")
            .setPositiveButton("Delete", (dialog, which) -> {
                try { store.delete(file); after.run(); } catch (Exception error) { toast(error); }
            }).setNegativeButton("Cancel", null).show();
    }

    @Override public void onBackPressed() {
        if (opening) return;
        if (running || runtimeView != null) { showProject(project); return; }
        if (editorFile != null) { if (saveEditor()) showDirectory(directory); return; }
        if (project != null) {
            if (directory != null && !directory.equals(project)) showDirectory(directory.getParentFile());
            else showProjects();
            return;
        }
        super.onBackPressed();
    }
    @Override protected void onDestroy() {
        stopRuntime();
        if (nativeAvailable) try { nativeVulkanDestroy(); } catch (Throwable ignored) { }
        super.onDestroy();
    }
}
