package com.luac.studio;

import static org.junit.Assert.*;
import org.junit.Test;

public class BlockCategoriesTest {
    @Test public void everyImplementedTypeAppearsExactlyOnceAndUsesItsCategoryColor() {
        String[] types = {"print", "printvar", "set", "textvar", "change", "multiply", "divide",
            "repeat", "if", "ifelse", "scene", "object", "position", "move", "size", "velocity"};
        boolean[] seen = new boolean[types.length];
        for (int category = 0; category < BlockCategories.COLORS.length; category++) {
            for (int index : BlockCategories.indices(category)) {
                assertTrue(index >= 0 && index < types.length);
                assertFalse(seen[index]);
                seen[index] = true;
                assertEquals(BlockCategories.COLORS[category], BlockCategories.color(types[index]));
            }
        }
        for (boolean included : seen) assertTrue(included);
        int[] copy = BlockCategories.indices(0);
        copy[0] = 99;
        assertEquals(0, BlockCategories.indices(0)[0]);
        try { BlockCategories.color("missing"); fail("unknown type"); }
        catch (IllegalArgumentException expected) { }
    }
}
