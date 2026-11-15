package com.luac.studio;

import android.content.Intent;
import android.content.Context;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
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
import java.io.OutputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.FileOutputStream;
import java.util.Locale;
import java.util.function.IntConsumer;
import org.json.JSONObject;

public final class MainActivity extends AppCompatActivity {
    private static final int PICK_ASSET = 41;
    private static final int IMPORT_PROJECT = 42;
    private static final int EXPORT_PROJECT = 43;
    private static final int BUILD_APK = 44;

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
    private File project, directory, editorFile, imageFile, importDirectory, exportProject, apkProject;
    private EditText editor;
    private TextView output;
    private RuntimeView runtimeView;
    private PaintView paintView;
    private boolean running, opening, runtimeError, destroyed, showingSamples;
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

    @Override protected void attachBaseContext(Context base) {
        Configuration config = new Configuration(base.getResources().getConfiguration());
        config.setLocale(new Locale(StudioTheme.language(base)));
        super.attachBaseContext(base.createConfigurationContext(config));
    }

    private String tr(String english, String russian) {
        return "ru".equals(StudioTheme.language(this)) ? russian : english;
    }

    @Override public void onCreate(Bundle state) {
        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES);
        super.onCreate(state);
        store = new ProjectStore(this);
        try { if (store.projects().isEmpty()) store.createProject("Demo"); }
        catch (Exception error) { toast(error); }
        showProjects();
        if (state != null) {
            String pending = state.getString("exportProject");
            if (pending != null) exportProject = new File(pending);
            String apkPending = state.getString("apkProject");
            if (apkPending != null) apkProject = new File(apkPending);
            String paintProject = state.getString("paintProject"), draft = state.getString("paintDraft");
            if (paintProject != null && draft != null) {
                File saved = new File(draft);
                try {
                    File restoredProject = new File(paintProject);
                    if (!restoredProject.isDirectory() || !restoredProject.getParentFile().getCanonicalFile().equals(store.root.getCanonicalFile()) ||
                        !saved.getParentFile().getCanonicalFile().equals(getCacheDir().getCanonicalFile()))
                        throw new IOException("Cannot restore drawing");
                    project = restoredProject;
                    directory = restoredProject;
                    showPaint();
                    paintView.restore(saved.getAbsolutePath(), state.getBoolean("paintDirty"));
                } catch (Exception error) { toast(error); }
                finally { if (saved.getParentFile().equals(getCacheDir())) saved.delete(); }
            }
        }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        if (exportProject != null) state.putString("exportProject", exportProject.getAbsolutePath());
        if (apkProject != null) state.putString("apkProject", apkProject.getAbsolutePath());
        if (paintView != null && project != null) {
            File draft = null;
            try {
                draft = File.createTempFile("paint-", ".png", getCacheDir());
                try (OutputStream out = new FileOutputStream(draft)) { paintView.writePng(out); }
                state.putString("paintProject", project.getAbsolutePath());
                state.putString("paintDraft", draft.getAbsolutePath());
                state.putBoolean("paintDirty", paintView.hasUnsavedChanges());
            } catch (IOException error) {
                if (draft != null) draft.delete();
                toast("Could not preserve the drawing: " + error.getMessage());
            }
        }
        super.onSaveInstanceState(state);
    }

    private void toast(Throwable error) { Toast.makeText(this, String.valueOf(error.getMessage() == null ? error : error.getMessage()), Toast.LENGTH_LONG).show(); }
    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }

    private MaterialToolbar toolbar(int layout) {
        setContentView(layout);
        MaterialToolbar bar = findViewById(R.id.toolbar);
        bar.setTitleTextColor(Color.WHITE);
        bar.setSubtitleTextColor(Color.WHITE);
        bar.post(() -> StudioTheme.apply(getWindow().getDecorView(), StudioTheme.color(this)));
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
        showingSamples = false;
        paintView = null;
        project = directory = editorFile = imageFile = null;
        MaterialToolbar bar = toolbar(R.layout.screen_projects);
        String version = "native unavailable";
        if (nativeAvailable) try { version = nativeVersion(); } catch (Throwable ignored) { }
        bar.setTitle("LuaC Studio");
        bar.setSubtitle(version + " • in-process runtime");
        bar.inflateMenu(R.menu.menu_projects);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_import_project) { pickProject(); return true; }
            if (item.getItemId() == R.id.action_samples) { showSamples(); return true; }
            if (item.getItemId() == R.id.action_settings) { showSettings(); return true; }
            return false;
        });
        RecyclerView list = findViewById(R.id.list);
        list.setLayoutManager(new LinearLayoutManager(this));
        ProjectAdapter adapter = new ProjectAdapter(store, store.projects(), new ProjectAdapter.Listener() {
            @Override public void onOpen(File item) { showProject(item); }
            @Override public void onRun(File item) { startProject(item); }
            @Override public void onRename(File item) {
                renameDialog(item, renamed -> { showProjects(); toast(tr("Renamed to ", "Переименовано в ") + renamed.getName()); });
            }
            @Override public void onExport(File item) { exportProject(item); }
            @Override public void onDelete(File item) { confirmDelete(item, MainActivity.this::showProjects); }
        });
        list.setAdapter(adapter);
        ExtendedFloatingActionButton fab = findViewById(R.id.fab);
        fab.setOnClickListener(v -> newProjectDialog());
    }

    private void showSettings() {
        String current = StudioTheme.language(this).equals("ru") ? "Русский" : "English";
        String[] choices = {tr("Language", "Язык") + ": " + current,
                            tr("Theme color", "Цвет темы")};
        new MaterialAlertDialogBuilder(this).setTitle(tr("Settings", "Настройки"))
            .setItems(choices, (dialog, which) -> {
                if (which == 0) showLanguageDialog();
                else showColorDialog(tr("Theme color", "Цвет темы"), StudioTheme.color(this), true, color -> {
                    StudioTheme.save(this, color);
                    StudioTheme.apply(getWindow().getDecorView(), color);
                });
            }).setNegativeButton(tr("Close", "Закрыть"), null).show();
    }

    private void showLanguageDialog() {
        String[] languages = {"English", "Русский"};
        int selected = StudioTheme.language(this).equals("ru") ? 1 : 0;
        new MaterialAlertDialogBuilder(this).setTitle(tr("Language", "Язык"))
            .setSingleChoiceItems(languages, selected, (dialog, which) -> {
                dialog.dismiss();
                String language = which == 1 ? "ru" : "en";
                if (!language.equals(StudioTheme.language(this))) {
                    StudioTheme.saveLanguage(this, language);
                    recreate();
                }
            }).setNegativeButton(tr("Cancel", "Отмена"), null).show();
    }

    private void showColorDialog(String title, int initial, boolean reset, IntConsumer onApply) {
        int padding = (int) (20 * getResources().getDisplayMetrics().density);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, 0, padding, padding);
        ColorWheelView wheel = new ColorWheelView(this, initial);
        content.addView(wheel, new LinearLayout.LayoutParams(-1, -2));
        EditText hex = new EditText(this);
        hex.setSingleLine(true);
        hex.setTextColor(Color.WHITE);
        hex.setHintTextColor(Color.WHITE);
        hex.setHint("#RRGGBB");
        hex.setContentDescription(tr("Hex color", "Цвет в HEX"));
        hex.setFilters(new InputFilter[] {new InputFilter.LengthFilter(7)});
        hex.setText(ColorHex.format(initial));
        content.addView(hex, new LinearLayout.LayoutParams(-1, -2));
        TextView preview = new TextView(this);
        preview.setTextColor(Color.WHITE);
        preview.setPadding(padding, padding / 2, padding, padding / 2);
        content.addView(preview, new LinearLayout.LayoutParams(-1, -2));
        IntConsumer update = color -> {
            preview.setText(ColorHex.format(color));
            preview.setBackgroundColor(StudioTheme.dark(color));
        };
        update.accept(initial);
        final boolean[] updating = {false};
        wheel.setListener(color -> {
            updating[0] = true;
            hex.setText(ColorHex.format(color));
            hex.setSelection(hex.length());
            hex.setError(null);
            updating[0] = false;
            update.accept(color);
        });
        hex.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                if (updating[0]) return;
                Integer color = ColorHex.parse(s.toString());
                if (color != null) { hex.setError(null); wheel.setColor(color); update.accept(color); }
            }
            @Override public void afterTextChanged(Editable value) { }
        });
        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        MaterialAlertDialogBuilder builder = new MaterialAlertDialogBuilder(this)
            .setTitle(title).setView(scroll)
            .setPositiveButton(tr("Apply", "Применить"), null)
            .setNegativeButton(tr("Cancel", "Отмена"), null);
        if (reset) builder.setNeutralButton(tr("Default", "По умолчанию"), (d, which) -> onApply.accept(StudioTheme.defaultColor()));
        androidx.appcompat.app.AlertDialog dialog = builder.create();
        dialog.setOnShowListener(unused -> dialog.getButton(androidx.appcompat.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            Integer color = ColorHex.parse(hex.getText().toString());
            if (color == null) {
                hex.setError(tr("Use six hex digits, for example #FF8040", "Введите шесть HEX-цифр, например #FF8040"));
                hex.requestFocus();
                return;
            }
            onApply.accept(color);
            dialog.dismiss();
        }));
        dialog.show();
    }

    private void showSamples() {
        showingSamples = true;
        MaterialToolbar bar = toolbar(R.layout.screen_samples);
        bar.setTitle(tr("Packages", "Пакеты"));
        bar.setSubtitle(tr("Examples • create an editable copy", "Примеры • создайте копию для редактирования"));
        back(bar, this::showProjects);
        LinearLayout list = findViewById(R.id.samples);
        for (SampleCatalog.Sample sample : SampleCatalog.SAMPLES) {
            View card = LayoutInflater.from(this).inflate(R.layout.item_project, list, false);
            ((TextView) card.findViewById(R.id.name)).setText(sample.name);
            ((TextView) card.findViewById(R.id.subtitle)).setText(SampleCatalog.description(this, sample));
            MaterialButton create = card.findViewById(R.id.run);
            create.setText(tr("Create", "Создать"));
            create.setIcon(null);
            card.findViewById(R.id.more).setVisibility(View.GONE);
            View.OnClickListener install = v -> {
                try { showingSamples = false; showProject(SampleCatalog.install(getAssets(), store, sample)); }
                catch (Exception error) { showingSamples = true; toast(error); }
            };
            create.setOnClickListener(install);
            card.setOnClickListener(install);
            list.addView(card);
        }
    }

    private void newProjectDialog() {
        nameDialog(tr("New project", "Новый проект"), "", "Project name", tr("Create", "Создать"), name -> {
            try { showProject(store.createProject(name)); }
            catch (Exception error) { toast(error); }
        });
    }

    private void showProject(File selected) { project = selected; showDirectory(selected); }

    private void showDirectory(File selected) {
        stopRuntime();
        paintView = null;
        directory = selected;
        editorFile = imageFile = null;
        MaterialToolbar bar = toolbar(R.layout.screen_files);
        bar.setTitle(project.getName());
        String path = relative(selected);
        bar.setSubtitle(path.isEmpty() ? tr("Project files", "Файлы проекта") : path);
        back(bar, this::onBackPressed);
        bar.inflateMenu(R.menu.menu_files);
        bar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_run) startProject(project);
            else if (item.getItemId() == R.id.action_add_file) nameDialog(tr("New file", "Новый файл"), "", "script.lua", tr("Create", "Создать"), name -> {
                try { showEditor(store.createFile(directory, name)); } catch (Exception error) { toast(error); }
            });
            else if (item.getItemId() == R.id.action_add_folder) nameDialog(tr("New folder", "Новая папка"), "", "scripts", tr("Create", "Создать"), name -> {
                try { store.createFolder(directory, name); showDirectory(directory); } catch (Exception error) { toast(error); }
            });
            else if (item.getItemId() == R.id.action_import) pickAsset();
            else if (item.getItemId() == R.id.action_paint) showPaint();
            else if (item.getItemId() == R.id.action_export_project) exportProject(project);
            else if (item.getItemId() == R.id.action_build_apk) chooseApkDestination(project);
            return true;
        });
        RecyclerView list = findViewById(R.id.list);
        list.setLayoutManager(new LinearLayoutManager(this));
        FileAdapter adapter = new FileAdapter(store.children(directory), new FileAdapter.Listener() {
            @Override public void onOpen(File file) { openFile(file); }
            @Override public void onRename(File file) {
                renameDialog(file, renamed -> { showDirectory(directory); toast(tr("Renamed to ", "Переименовано в ") + renamed.getName()); });
            }
            @Override public void onDelete(File file) { confirmDelete(file, () -> showDirectory(directory)); }
        });
        list.setAdapter(adapter);
    }

    private void showPaint() {
        stopRuntime();
        editorFile = imageFile = null;
        MaterialToolbar bar = toolbar(R.layout.screen_paint);
        bar.setTitle("Paint");
        bar.setSubtitle(tr("512 × 512 • transparent PNG", "512 × 512 • прозрачный PNG"));
        back(bar, this::leavePaint);
        paintView = new PaintView(this);
        FrameLayout canvas = findViewById(R.id.paint_canvas);
        canvas.addView(paintView, new FrameLayout.LayoutParams(-1, -1));
        MaterialButton colorButton = findViewById(R.id.paint_color);
        colorButton.setOnClickListener(v -> showColorDialog(tr("Brush color", "Цвет кисти"), paintView.getBrushColor(), false, color -> {
            paintView.setBrushColor(color);
            colorButton.setText(tr("Color ", "Цвет ") + ColorHex.format(color));
        }));
        findViewById(R.id.paint_eraser).setOnClickListener(v -> { paintView.setEraser(); toast(tr("Eraser", "Ластик")); });
        findViewById(R.id.paint_undo).setOnClickListener(v -> paintView.undo());
        findViewById(R.id.paint_clear).setOnClickListener(v ->
            new MaterialAlertDialogBuilder(this).setTitle(tr("Clear the drawing?", "Очистить рисунок?"))
                .setPositiveButton(tr("Clear", "Очистить"), (dialog, which) -> paintView.clear())
                .setNegativeButton(tr("Cancel", "Отмена"), null).show());
        findViewById(R.id.paint_save).setOnClickListener(v -> savePainting());
        SeekBar size = findViewById(R.id.paint_size);
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seek, int progress, boolean fromUser) { paintView.setBrushSize(progress + 1); }
            @Override public void onStartTrackingTouch(SeekBar seek) { }
            @Override public void onStopTrackingTouch(SeekBar seek) { }
        });
    }

    private void savePainting() {
        nameDialog(tr("Save image to assets", "Сохранить рисунок в assets"), "drawing.png", "drawing.png", tr("Save", "Сохранить"), name -> {
            try {
                File saved = store.savePainting(project, name, output -> paintView.writePng(output));
                paintView.markSaved();
                toast(tr("Saved ", "Сохранено: ") + relative(saved));
            } catch (Exception error) { toast(error); }
        });
    }

    private void leavePaint() {
        if (paintView != null && paintView.hasUnsavedChanges()) {
            new MaterialAlertDialogBuilder(this).setTitle(tr("Discard unsaved drawing?", "Отменить несохранённый рисунок?"))
                .setMessage(tr("Save the image to assets before leaving, or discard your changes.", "Сохраните рисунок в assets или отмените изменения."))
                .setPositiveButton(tr("Discard", "Отменить изменения"), (dialog, which) -> showDirectory(directory))
                .setNegativeButton(tr("Keep drawing", "Продолжить рисовать"), null).show();
        } else showDirectory(directory);
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
        save.setOnClickListener(v -> { if (saveEditor()) toast(tr("Saved", "Сохранено")); });
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
            toast(tr("Renamed to ", "Переименовано в ") + renamed.getName());
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
        if (!new File(selected, "main.lua").isFile()) { toast(tr("main.lua is missing", "Файл main.lua не найден")); return; }
        if (!nativeAvailable) { toast("Native library unavailable: " + nativeLoadError); return; }
        opening = true;
        MaterialToolbar bar = toolbar(R.layout.screen_runtime);
        bar.setTitle(tr("Starting ", "Запуск: ") + selected.getName() + "…");
        bar.setSubtitle(tr("Loading main.lua, modules and textures", "Загрузка main.lua, модулей и текстур"));
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
                    new MaterialAlertDialogBuilder(this).setTitle(tr("Project could not start", "Не удалось запустить проект")).setMessage(completed)
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
        bar.setSubtitle(tr("Running • touch the scene", "Запущено • коснитесь сцены"));
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

    private void chooseApkDestination(File selected) {
        if (!new File(selected, "main.lua").isFile()) { toast(tr("main.lua is missing", "Файл main.lua не найден")); return; }
        new MaterialAlertDialogBuilder(this).setTitle(tr("Build standalone APK", "Собрать отдельный APK"))
            .setMessage(tr("The APK contains the project and LuaC runtime. Each project has its own app ID, stored in .luac/android-id. The signing key stays on this device; reinstalling Studio can prevent updates to previously built APKs.", "APK содержит проект и LuaC runtime. ID приложения хранится в .luac/android-id. Ключ подписи остаётся на устройстве; после переустановки Studio обновить старый APK может не получиться."))
            .setPositiveButton(tr("Choose destination", "Выбрать место"), (dialog, which) -> {
                apkProject = selected;
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/vnd.android.package-archive");
                intent.putExtra(Intent.EXTRA_TITLE, selected.getName() + ".apk");
                startActivityForResult(intent, BUILD_APK);
            }).setNegativeButton(tr("Cancel", "Отмена"), null).show();
    }

    private void buildApk(Uri destination, File selected) {
        toast(tr("Building and signing APK…", "Сборка и подпись APK…"));
        new Thread(() -> {
            File unsigned = null, signed = null;
            try {
                // Fail early if the project was deleted or moved while the picker was open.
                if (selected.getParentFile() == null || !selected.getParentFile().getCanonicalFile().equals(store.root.getCanonicalFile()))
                    throw new IOException("Project is outside the projects folder");
                unsigned = File.createTempFile("luac-unsigned-", ".apk", getCacheDir());
                signed = File.createTempFile("luac-signed-", ".apk", getCacheDir());
                ApkSigningKey signing = ApkSigningKey.load();
                try (InputStream template = getAssets().open("runner-template.apk")) {
                    ApkBuilder.build(template, selected, unsigned, signed, signing.key, signing.certificate);
                }
                try (InputStream input = new java.io.FileInputStream(signed);
                     OutputStream output = getContentResolver().openOutputStream(destination, "wt")) {
                    if (output == null) throw new IOException("Cannot write the APK destination");
                    byte[] buffer = new byte[65536];
                    int count;
                    while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                }
                handler.post(() -> { if (!destroyed) toast(tr("Signed project APK saved", "Подписанный APK проекта сохранён")); });
            } catch (Exception error) {
                handler.post(() -> { if (!destroyed) toast(error); });
            } finally {
                if (unsigned != null) unsigned.delete();
                if (signed != null) signed.delete();
            }
        }, "LuaC-apk-builder").start();
    }

    private void pickProject() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // .luac is not registered with Android's MIME database.
        intent.setType("*/*");
        startActivityForResult(intent, IMPORT_PROJECT);
    }

    private void exportProject(File selected) {
        exportProject = selected;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/json");
        intent.putExtra(Intent.EXTRA_TITLE, selected.getName() + ".luac");
        startActivityForResult(intent, EXPORT_PROJECT);
    }

    private void importProject(Uri uri) {
        new Thread(() -> {
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IOException("Cannot open selected project");
                File imported = store.importProject(input);
                handler.post(() -> { if (!destroyed) { showProject(imported); toast(tr("Imported ", "Импортировано: ") + imported.getName()); } });
            } catch (Exception error) {
                handler.post(() -> { if (!destroyed) toast(error); });
            }
        }, "LuaC-project-import").start();
    }

    private void writeProject(Uri uri, File selected) {
        new Thread(() -> {
            try {
                ByteArrayOutputStream archive = new ByteArrayOutputStream();
                store.exportProject(selected, archive);
                try (OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                    if (output == null) throw new IOException("Cannot write selected destination");
                    archive.writeTo(output);
                    output.flush();
                }
                handler.post(() -> { if (!destroyed) toast(tr("Project exported as .luac", "Проект экспортирован в .luac")); });
            } catch (Exception error) {
                handler.post(() -> { if (!destroyed) toast(error); });
            }
        }, "LuaC-project-export").start();
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
        if (request == BUILD_APK) {
            File selected = apkProject; apkProject = null;
            if (result == RESULT_OK && data != null && data.getData() != null && selected != null)
                buildApk(data.getData(), selected);
            return;
        }
        if (request == EXPORT_PROJECT) {
            File selected = exportProject;
            exportProject = null;
            if (result == RESULT_OK && data != null && data.getData() != null && selected != null)
                writeProject(data.getData(), selected);
            return;
        }
        if (request == IMPORT_PROJECT) {
            if (result == RESULT_OK && data != null && data.getData() != null) importProject(data.getData());
            return;
        }
        if (request != PICK_ASSET || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            File imported = store.importFile(importDirectory, displayName(uri), input);
            toast(tr("Imported ", "Импортировано: ") + imported.getName());
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
            .setNegativeButton(tr("Cancel", "Отмена"), null).show();
    }
    private void renameDialog(File file, RenameAction after) {
        String title = file.getName().equals("main.lua")
            ? tr("Rename entry point (main.lua is required to run)", "Переименовать точку входа (для запуска нужен main.lua)")
            : tr("Rename (references are not updated)", "Переименовать (пути не обновляются)");
        nameDialog(title, file.getName(), file.getName(), tr("Rename", "Переименовать"), name -> {
            try {
                File renamed = store.rename(file, name);
                if (renamed.equals(file)) { toast(tr("Name unchanged", "Имя не изменилось")); return; }
                after.run(renamed);
            } catch (Exception error) { toast(error); }
        });
    }
    private interface NameAction { void run(String name); }
    private interface RenameAction { void run(File renamed); }

    private void confirmDelete(File file, Runnable after) {
        new MaterialAlertDialogBuilder(this).setTitle(tr("Delete ", "Удалить ") + file.getName() + "?")
            .setMessage(file.isDirectory() ? tr("The folder and everything inside it will be deleted.", "Папка и всё её содержимое будут удалены.") : tr("This cannot be undone.", "Это действие нельзя отменить."))
            .setPositiveButton(tr("Delete", "Удалить"), (dialog, which) -> {
                try { store.delete(file); after.run(); } catch (Exception error) { toast(error); }
            }).setNegativeButton(tr("Cancel", "Отмена"), null).show();
    }

    @Override public void onBackPressed() {
        if (opening) return;
        if (showingSamples) { showProjects(); return; }
        if (paintView != null) { leavePaint(); return; }
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
