package com.luac.studio;

import static androidx.test.espresso.action.ViewActions.closeSoftKeyboard;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.*;

import android.content.Context;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class DeviceSettingsTest {
    @Test public void languageAndHexColorPersist() {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        String beforeLanguage = StudioTheme.language(context);
        int beforeColor = StudioTheme.color(context);
        StudioTheme.saveLanguage(context, "en");
        try (ActivityScenario<MainActivity> activity = ActivityScenario.launch(MainActivity.class)) {
            onView(withId(R.id.action_settings)).perform(click());
            onView(withText("Language: English")).perform(click());
            onView(withText("Русский")).perform(click());
            onView(withId(R.id.fab)).check(matches(withText("Новый проект")));
            assertEquals("ru", StudioTheme.language(context));

            onView(withId(R.id.action_settings)).perform(click());
            onView(withText("Цвет темы")).perform(click());
            onView(withContentDescription("Цвет в HEX")).perform(replaceText("#ABZZ00"), closeSoftKeyboard());
            onView(withText("Применить")).perform(click());
            onView(withContentDescription("Цвет в HEX")).check(matches(isDisplayed()));
            assertEquals(beforeColor, StudioTheme.color(context));
            onView(withContentDescription("Цвет в HEX")).perform(replaceText("#FF8040"), closeSoftKeyboard());
            onView(withText("Применить")).perform(click());
            assertEquals(0xffff8040, StudioTheme.color(context));
            activity.recreate();
            onView(withId(R.id.fab)).check(matches(withText("Новый проект")));
            assertEquals(0xffff8040, StudioTheme.color(context));

            onView(withText("Demo")).perform(click());
            activity.onActivity(screen -> {
                com.google.android.material.appbar.MaterialToolbar bar = screen.findViewById(R.id.toolbar);
                assertTrue(bar.getMenu().performIdentifierAction(R.id.action_paint, 0));
            });
            onView(withId(R.id.paint_color)).perform(click());
            onView(withContentDescription("Цвет в HEX")).perform(replaceText("#1888CC"), closeSoftKeyboard());
            onView(withText("Применить")).perform(click());
            onView(withId(R.id.paint_color)).check(matches(withText("Цвет #1888CC")));
            activity.onActivity(screen -> {
                android.widget.FrameLayout canvas = screen.findViewById(R.id.paint_canvas);
                assertEquals(0xff1888cc, ((PaintView) canvas.getChildAt(0)).getBrushColor());
            });
        } finally {
            StudioTheme.saveLanguage(context, beforeLanguage);
            StudioTheme.save(context, beforeColor);
        }
        assertEquals(Integer.valueOf(0xffff8040), ColorHex.parse("ff8040"));
        assertNull(ColorHex.parse("#xyz123"));
        assertNull(ColorHex.parse("#123"));
    }
}
