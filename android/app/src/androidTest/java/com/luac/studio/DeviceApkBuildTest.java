package com.luac.studio;

import static org.junit.Assert.*;
import android.content.Context;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Exercise the actual Android Keystore and apksig, not a host-only substitute. */
@RunWith(AndroidJUnit4.class)
public class DeviceApkBuildTest {
    @Test public void signsRunnableProjectOnDevice() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        File project = new File(context.getCacheDir(), "apk-device-project");
        if (project.exists()) ProjectFiles.delete(project);
        assertTrue(new File(project, "scripts").mkdirs());
        try (FileOutputStream out = new FileOutputStream(new File(project, "main.lua"))) {
            out.write(("print(require('scripts.message'))\nworld = engine.world()\n" +
                "engine.text(world, 'APK scene ready', 20, 40, 18)\n").getBytes(StandardCharsets.UTF_8));
        }
        try (FileOutputStream out = new FileOutputStream(new File(project, "scripts/message.lua"))) {
            out.write("return 'PROJECT APK DEVICE OK'\n".getBytes(StandardCharsets.UTF_8));
        }
        File unsigned = new File(context.getCacheDir(), "device-unsigned.apk");
        File signed = new File(context.getFilesDir(), "device-project.apk");
        ApkSigningKey key = ApkSigningKey.load();
        try (InputStream template = context.getAssets().open("runner-template.apk")) {
            ApkBuilder.build(template, project, unsigned, signed, key.key, key.certificate);
        }
        assertTrue(signed.length() > 0);
        unsigned.delete();
        ProjectFiles.delete(project);
    }
}
