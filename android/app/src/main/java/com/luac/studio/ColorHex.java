package com.luac.studio;

import java.util.Locale;

final class ColorHex {
    private ColorHex() { }
    static Integer parse(String text) {
        if (text == null) return null;
        String value = text.trim();
        if (value.startsWith("#")) value = value.substring(1);
        if (!value.matches("[0-9a-fA-F]{6}")) return null;
        return 0xff000000 | Integer.parseInt(value, 16);
    }
    static String format(int color) { return String.format(Locale.ROOT, "#%06X", color & 0xffffff); }
}
