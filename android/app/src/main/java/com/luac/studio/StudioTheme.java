package com.luac.studio;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.widget.SeekBar;
import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;

final class StudioTheme {
    private static final String PREFS = "studio-settings";
    private static final int DEFAULT = 0xff4bdcb5;
    private StudioTheme() { }
    static int color(Context context) { return context.getSharedPreferences(PREFS, 0).getInt("accent", DEFAULT); }
    static void save(Context context, int color) { context.getSharedPreferences(PREFS, 0).edit().putInt("accent", color | 0xff000000).apply(); }
    static int defaultColor() { return DEFAULT; }
    static int dark(int color) {
        float[] hsv = new float[3]; Color.colorToHSV(color, hsv);
        hsv[2] = Math.min(hsv[2], .34f);
        return Color.HSVToColor(hsv);
    }
    static void apply(View view, int color) {
        if (view instanceof MaterialToolbar) view.setBackgroundColor(dark(color));
        if (view instanceof MaterialButton) {
            MaterialButton button = (MaterialButton) view;
            if (button.getText().length() > 0) {
                button.setBackgroundTintList(ColorStateList.valueOf(dark(color)));
                button.setTextColor(Color.WHITE);
                button.setIconTint(ColorStateList.valueOf(Color.WHITE));
            }
        }
        if (view instanceof SeekBar) {
            ((SeekBar) view).setProgressTintList(ColorStateList.valueOf(color));
            ((SeekBar) view).setThumbTintList(ColorStateList.valueOf(color));
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) apply(group.getChildAt(i), color);
        }
    }
}
