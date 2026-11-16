package com.luac.studio;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.closeSoftKeyboard;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.*;
import static org.junit.Assert.*;
import android.content.Context;
import android.widget.TextView;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class DeviceSceneTest {
    @Test public void backgroundIsRenderedBehindActor() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        File project = new File(context.getCacheDir(), "layer-test");
        assertTrue(project.mkdirs() || project.isDirectory());
        File image = new File(project, "red.png");
        android.graphics.Bitmap red = android.graphics.Bitmap.createBitmap(2, 2, android.graphics.Bitmap.Config.ARGB_8888);
        red.eraseColor(android.graphics.Color.RED);
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(image)) {
            assertTrue(red.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out));
        }
        red.recycle();
        final int[] pixels = new int[2];
        try (ActivityScenario<MainActivity> activity = ActivityScenario.launch(MainActivity.class)) {
            activity.onActivity(screen -> {
                try {
                    RuntimeView view = new RuntimeView(screen, project, (id, x, y, down) -> {});
                    view.layout(0, 0, 100, 100);
                    org.json.JSONObject actor = new org.json.JSONObject().put("width", 40).put("height", 40)
                        .put("color", new org.json.JSONArray().put(0).put(0).put(1).put(1));
                    org.json.JSONObject background = new org.json.JSONObject().put("z", -1)
                        .put("width", 100).put("height", 100).put("texture", "red.png");
                    view.setFrame(new org.json.JSONObject().put("entities", new org.json.JSONArray().put(actor).put(background)));
                    android.graphics.Bitmap target = android.graphics.Bitmap.createBitmap(100, 100, android.graphics.Bitmap.Config.ARGB_8888);
                    view.draw(new android.graphics.Canvas(target));
                    pixels[0] = target.getPixel(50, 50); pixels[1] = target.getPixel(5, 5);
                    target.recycle();
                } catch (org.json.JSONException error) { throw new AssertionError(error); }
            });
            assertEquals(android.graphics.Color.BLUE, pixels[0]);
            assertEquals(android.graphics.Color.RED, pixels[1]);
        } finally { image.delete(); project.delete(); }
    }

    @Test public void actorScriptPersistsAndRuns() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        String language = StudioTheme.language(context);
        boolean mode = StudioTheme.blocks(context);
        StudioTheme.saveLanguage(context, "en"); StudioTheme.saveBlocks(context, false);
        ProjectStore store = new ProjectStore(context);
        if (!new File(store.root, "Demo").exists()) store.createProject("Demo");
        File project = store.createSceneProject("Scene UI");
        try (ActivityScenario<MainActivity> activity = ActivityScenario.launch(MainActivity.class)) {
            onView(withText(project.getName())).perform(click());
            onView(withText("Background")).check(matches(isDisplayed()));
            onView(withId(R.id.scene_add)).perform(click());
            onView(withId(R.id.input)).perform(replaceText("Hero"), closeSoftKeyboard());
            onView(withText("Create")).perform(click());
            onView(withText("Hero")).perform(click());
            onView(withText("Scripts")).perform(click());
            onView(withId(R.id.blocks_add)).perform(click());
            onView(withText("Output")).perform(click());
            onView(withText("Print text")).perform(click());
            onView(withContentDescription("Text")).perform(replaceText("scene actor works"), closeSoftKeyboard());
            onView(withText("Save")).perform(click());
            onView(withId(R.id.save)).perform(click());
            pressBack();
            onView(withText("Scripts")).check(matches(isDisplayed()));
            pressBack();
            SceneProject scene = SceneProject.open(store, project);
            String id = scene.actors().getJSONObject(0).getString("id");
            assertTrue(store.read(scene.script(id)).contains("scene actor works"));
            activity.onActivity(screen -> screen.findViewById(R.id.action_run).performClick());
            final String[] output = {""};
            for (int i = 0; i < 40; i++) {
                activity.onActivity(screen -> { TextView view = screen.findViewById(R.id.output); if (view != null) output[0] = view.getText().toString(); });
                if (output[0].contains("scene actor works")) break;
                Thread.sleep(500);
            }
            assertTrue(output[0], output[0].contains("scene actor works"));
            activity.onActivity(MainActivity::onBackPressed);
            onView(withText("Hero")).check(matches(isDisplayed()));
        } finally {
            StudioTheme.saveLanguage(context, language); StudioTheme.saveBlocks(context, mode);
            store.delete(project);
        }
    }
}
