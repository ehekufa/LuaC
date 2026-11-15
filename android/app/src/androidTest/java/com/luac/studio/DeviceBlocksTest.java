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
            onView(withId(R.id.run)).perform(click());
            final String[] output = {""};
            for (int i = 0; i < 40; i++) {
                activity.onActivity(screen -> { TextView view = screen.findViewById(R.id.output); if (view != null) output[0] = view.getText().toString(); });
                if (output[0].contains("block output") && output[0].contains("existing script")) break;
                Thread.sleep(500);
            }
            assertTrue(output[0], output[0].contains("block output") && output[0].contains("existing script"));
            pressBack();
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
}
