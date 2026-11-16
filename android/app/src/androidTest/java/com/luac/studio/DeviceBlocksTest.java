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
public class DeviceBlocksTest {
    @Test public void blocksPersistAndRunAlongsideExistingScript() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        String language = StudioTheme.language(context);
        boolean mode = StudioTheme.blocks(context);
        StudioTheme.saveLanguage(context, "en"); StudioTheme.saveBlocks(context, false);
        ProjectStore store = new ProjectStore(context);
        if (!new File(store.root, "Demo").exists()) store.createProject("Demo");
        File project = store.createProject("Blocks UI");
        File main = new File(project, "main.lua");
        String original = "print('existing script')\n";
        store.write(main, original);
        try (ActivityScenario<MainActivity> activity = ActivityScenario.launch(MainActivity.class)) {
            onView(withId(R.id.action_settings)).perform(click());
            onView(withText("Editor mode: Script")).perform(click());
            onView(withText("Blocks")).perform(click());
            assertTrue(StudioTheme.blocks(context));
            activity.recreate();
            onView(withText(project.getName())).perform(click());
            onView(withText("main.lua")).perform(click());
            onView(withId(R.id.blocks_add)).perform(click());
            onView(withText("Print text")).perform(click());
            onView(withContentDescription("Text")).perform(replaceText("block output"), closeSoftKeyboard());
            onView(withText("Save")).perform(click());
            onView(withText("Print: block output")).check(matches(isDisplayed()));
            onView(withId(R.id.save)).perform(click());
            String saved = store.read(main);
            assertTrue(saved.contains("print(\"block output\")"));
            assertTrue(saved.endsWith(original));
            assertEquals(1, BlockProgram.open(saved).blocks.length());
            // The running scene renders continuously, so Espresso cannot wait for an idle frame.
            activity.onActivity(screen -> screen.findViewById(R.id.run).performClick());
            final String[] output = {""};
            for (int i = 0; i < 40; i++) {
                activity.onActivity(screen -> { TextView view = screen.findViewById(R.id.output); if (view != null) output[0] = view.getText().toString(); });
                if (output[0].contains("block output") && output[0].contains("existing script")) break;
                Thread.sleep(500);
            }
            assertTrue(output[0], output[0].contains("block output") && output[0].contains("existing script"));
            activity.onActivity(MainActivity::onBackPressed);
            onView(withText("main.lua")).perform(click());
            onView(withText("Print: block output")).check(matches(isDisplayed()));
            pressBack(); pressBack();
            onView(withId(R.id.action_settings)).perform(click());
            onView(withText("Editor mode: Blocks")).perform(click());
            onView(withText("Script")).perform(click());
            onView(withText(project.getName())).perform(click());
            onView(withText("main.lua")).perform(click());
            onView(withId(R.id.editor)).check(matches(isDisplayed()));
            assertEquals(saved, store.read(main));
        } finally {
            StudioTheme.saveLanguage(context, language); StudioTheme.saveBlocks(context, mode);
            store.delete(project);
        }
    }
    @Test public void draggingReordersBlocks() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        BlockProgram document = BlockProgram.open("");
        document.blocks.put(new org.json.JSONObject().put("type", "print").put("text", "A"));
        document.blocks.put(new org.json.JSONObject().put("type", "print").put("text", "B"));
        final int[][] positions = {null, null};
        final BlockEditorView[] editorView = {null};
        try (ActivityScenario<MainActivity> activity = ActivityScenario.launch(MainActivity.class)) {
            activity.onActivity(screen -> {
                BlockEditorView view = new BlockEditorView(screen, document, false);
                editorView[0] = view;
                screen.setContentView(view);
                // Keep both endpoints on even the smallest emulator screen.
                android.widget.LinearLayout content = (android.widget.LinearLayout) view.getChildAt(0);
                content.getChildAt(0).setVisibility(android.view.View.GONE);
                content.getChildAt(1).setVisibility(android.view.View.GONE);
                java.util.ArrayList<android.view.View> headers = new java.util.ArrayList<>();
                view.findViewsWithText(headers, "Hold and drag: Print:", android.view.View.FIND_VIEWS_WITH_CONTENT_DESCRIPTION);
                for (android.view.View header : headers) {
                    android.widget.LinearLayout card = (android.widget.LinearLayout) header.getParent();
                    card.getChildAt(1).setVisibility(android.view.View.GONE);
                }
                view.post(() -> {
                    java.util.ArrayList<android.view.View> found = new java.util.ArrayList<>();
                    view.findViewsWithText(found, "Hold and drag: Print: A", android.view.View.FIND_VIEWS_WITH_CONTENT_DESCRIPTION);
                    if (found.isEmpty()) return;
                    android.view.View title = found.get(0), target = view.findViewWithTag("block-drop-0-2");
                    if (target == null) return;
                    android.graphics.Rect visible = new android.graphics.Rect();
                    if (!target.getGlobalVisibleRect(visible)) return;
                    int[] a = new int[2], b = new int[2];
                    title.getLocationOnScreen(a); target.getLocationOnScreen(b);
                    positions[0] = new int[] {a[0] + title.getWidth() / 2, a[1] + title.getHeight() / 2};
                    positions[1] = new int[] {b[0] + target.getWidth() / 2, b[1] + target.getHeight() / 2};
                });
            });
            InstrumentationRegistry.getInstrumentation().waitForIdleSync();
            assertNotNull(positions[0]); assertNotNull(positions[1]);
            onView(withContentDescription("Hold and drag: Print: A")).check(matches(isDisplayed()));
            long down = android.os.SystemClock.uptimeMillis();
            android.app.Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
            send(instrumentation, down, android.view.MotionEvent.ACTION_DOWN, positions[0][0], positions[0][1]);
            android.os.SystemClock.sleep(900);
            for (int i = 1; i <= 12; i++) {
                float t = i / 12f;
                send(instrumentation, down, android.view.MotionEvent.ACTION_MOVE,
                    positions[0][0] + t * (positions[1][0] - positions[0][0]),
                    positions[0][1] + t * (positions[1][1] - positions[0][1]));
                android.os.SystemClock.sleep(30);
            }
            send(instrumentation, down, android.view.MotionEvent.ACTION_UP, positions[1][0], positions[1][1]);
            instrumentation.waitForIdleSync();
            assertEquals("trace=" + editorView[0].dragTrace + " positions=" + java.util.Arrays.deepToString(positions),
                "B", document.blocks.getJSONObject(0).getString("text"));
            assertEquals("A", document.blocks.getJSONObject(1).getString("text"));
            assertTrue(document.dirty);
        }
    }
    private static void send(android.app.Instrumentation instrumentation, long down, int action, float x, float y) {
        android.view.MotionEvent event = android.view.MotionEvent.obtain(down, android.os.SystemClock.uptimeMillis(), action, x, y, 0);
        try { assertTrue(instrumentation.getUiAutomation().injectInputEvent(event, true)); }
        finally { event.recycle(); }
    }
}
