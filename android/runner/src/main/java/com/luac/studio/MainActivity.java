package com.luac.studio;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONObject;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.security.DigestInputStream;
import java.security.MessageDigest;

/** A small standalone host; the Lua sources are supplied when Studio builds the APK. */
public final class MainActivity extends Activity {
    private static native String nativeOpenProject(String root);
    private static native String nativeStepProject(double dt);
    private static native void nativeTouchProject(int id, double x, double y, boolean down);
    private static native void nativeCloseProject();
    // The JNI runtime owns one native project per process. Activity recreation
    // must not let an old loader close or step a newer activity's project.
    private static final Object SESSION_LOCK = new Object();
    private static final Object INSTALL_LOCK = new Object();
    private static MainActivity owner;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final StringBuilder log = new StringBuilder();
    private volatile boolean destroyed;
    private boolean opened, resumed, failed;
    private long previous;
    private RuntimeView runtime;
    private FrameLayout scene;
    private TextView output;
    private final Runnable frame = new Runnable() {
        @Override public void run() {
            if (!opened || !resumed || failed) return;
            long now = System.nanoTime();
            double dt = previous == 0 ? 1.0 / 60 : Math.min(.05, (now - previous) / 1e9);
            previous = now;
            try {
                synchronized (SESSION_LOCK) {
                    if (owner != MainActivity.this) return;
                    apply(new JSONObject(nativeStepProject(dt)));
                }
            } catch (Throwable error) { fail("Runtime error: " + error); }
            if (!failed) handler.postDelayed(this, 16);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        synchronized (SESSION_LOCK) {
            if (owner != null) {
                try { nativeCloseProject(); } catch (UnsatisfiedLinkError ignored) { }
            }
            owner = this;
        }
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(7, 13, 20));
        scene = new FrameLayout(this);
        root.addView(scene, new LinearLayout.LayoutParams(-1, 0, 1));
        output = new TextView(this);
        output.setTextColor(Color.WHITE);
        output.setText("Starting project…\n");
        output.setPadding(16, 12, 16, 12);
        ScrollView console = new ScrollView(this);
        console.addView(output);
        root.addView(console, new LinearLayout.LayoutParams(-1, (int) (110 * getResources().getDisplayMetrics().density)));
        setContentView(root);
        new Thread(this::startProject, "LuaC-project-loader").start();
    }

    private void startProject() {
        try {
            System.loadLibrary("luac_native");
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            ProjectArchive.Document document;
            try (InputStream raw = getAssets().open("luac-project.luac");
                 DigestInputStream input = new DigestInputStream(raw, digest)) {
                document = ProjectArchive.read(input);
            }
            StringBuilder id = new StringBuilder("project-");
            for (byte b : digest.digest()) id.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            File project = new File(getFilesDir(), id.toString());
            synchronized (INSTALL_LOCK) {
                if (!project.isDirectory()) {
                    File staging = Files.createTempDirectory(getFilesDir().toPath(), ".install-").toFile();
                    try {
                        ProjectFiles.write(document, staging);
                        if (!new File(staging, "main.lua").isFile()) throw new java.io.IOException("main.lua is missing");
                        Files.move(staging.toPath(), project.toPath());
                    } catch (Exception error) {
                        try { ProjectFiles.delete(staging); } catch (Exception cleanup) { error.addSuppressed(cleanup); }
                        throw error;
                    }
                }
            }
            final String initial;
            synchronized (SESSION_LOCK) {
                if (destroyed || owner != this) return;
                initial = nativeOpenProject(project.getAbsolutePath());
            }
            handler.post(() -> {
                synchronized (SESSION_LOCK) {
                    if (owner != this) return;
                    if (destroyed) { nativeCloseProject(); owner = null; return; }
                    opened = true;
                }
                runtime = new RuntimeView(this, project, (id1, x, y, down) -> {
                    synchronized (SESSION_LOCK) {
                        if (owner == this && opened && !failed) nativeTouchProject(id1, x, y, down);
                    }
                });
                scene.addView(runtime, new FrameLayout.LayoutParams(-1, -1, Gravity.CENTER));
                try { apply(new JSONObject(initial)); }
                catch (Exception error) { fail("Could not start project: " + initial); }
                if (resumed && !failed) handler.post(frame);
            });
        } catch (Throwable error) {
            handler.post(() -> { if (!destroyed) fail("Could not start project: " + error); });
        }
    }
    private void apply(JSONObject value) {
        runtime.setFrame(value);
        append(value.optString("output", ""));
        if (!value.optBoolean("ok", false)) fail(value.optString("error", "Runtime stopped"));
    }
    private void fail(String message) { failed = true; append("\n" + message + "\n"); }
    private void append(String text) {
        if (text == null || text.isEmpty()) return;
        log.append(text);
        if (log.length() > 100000) log.delete(0, log.length() - 80000);
        output.setText(log.toString());
    }
    @Override protected void onResume() {
        super.onResume(); resumed = true; previous = 0;
        if (opened && !failed) handler.post(frame);
    }
    @Override protected void onPause() {
        resumed = false; handler.removeCallbacks(frame); previous = 0;
        super.onPause();
    }
    @Override protected void onDestroy() {
        destroyed = true; handler.removeCallbacks(frame);
        synchronized (SESSION_LOCK) {
            if (owner == this) {
                owner = null;
                // If a loader has returned but not posted yet, its native session
                // still belongs to this activity and needs to be closed.
                try { nativeCloseProject(); } catch (UnsatisfiedLinkError ignored) { }
            }
            opened = false;
        }
        if (runtime != null) runtime.dispose();
        super.onDestroy();
    }
}
