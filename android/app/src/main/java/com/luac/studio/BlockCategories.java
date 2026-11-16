package com.luac.studio;

/** Categories correspond to implemented block types, not planned features. */
final class BlockCategories {
    static final int[] COLORS = {0xff78521b, 0xff285b55, 0xff31507b, 0xff694478};
    private static final int[][] INDICES = {{0, 1}, {2, 3, 4, 5, 6}, {7, 8, 9}, {10, 11, 12, 13, 14, 15}};

    static int[] indices(int category) { return INDICES[category].clone(); }

    static int color(String type) {
        switch (type) {
            case "print": case "printvar": return COLORS[0];
            case "set": case "textvar": case "change": case "multiply": case "divide": return COLORS[1];
            case "repeat": case "if": case "ifelse": return COLORS[2];
            case "scene": case "object": case "position": case "move": case "size": case "velocity": return COLORS[3];
            default: throw new IllegalArgumentException("Unknown block type: " + type);
        }
    }
    private BlockCategories() { }
}
