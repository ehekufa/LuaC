package com.luac.studio;

import static org.junit.Assert.*;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;

public class BlockProgramTest {
    @Test public void nestedBlocksRoundTripWithoutLosingOriginal() throws Exception {
        String original = "local name = 'Привет'\nreturn name\n";
        BlockProgram program = BlockProgram.open(original);
        JSONArray nested = new JSONArray().put(new JSONObject().put("type", "print").put("text", "hello \"\nмир"));
        JSONArray insideRepeat = new JSONArray().put(new JSONObject().put("type", "if").put("name", "score")
            .put("op", ">=").put("value", "2").put("children", nested));
        program.blocks.put(new JSONObject().put("type", "set").put("name", "score").put("value", "2"));
        program.blocks.put(new JSONObject().put("type", "repeat").put("value", "3").put("children", insideRepeat));
        String source = program.render();
        assertTrue(source.contains("score = 2\nfor _luac_block_i = 1, 3 do\n  if score >= 2 then\n"));
        assertTrue(source.contains("print(\"hello \\\"\\010мир\")"));
        assertTrue(source.endsWith(original));
        BlockProgram loaded = BlockProgram.open(source);
        assertFalse(loaded.recovered);
        assertEquals(original, loaded.original);
        assertEquals(source, loaded.render());
    }
    @Test public void scriptEditsAndBrokenMetadataAreNeverDiscarded() throws Exception {
        BlockProgram program = BlockProgram.open("return 42\n");
        program.blocks.put(new JSONObject().put("type", "print").put("text", "original"));
        String edited = program.render().replace("print(\"original\")", "print(\"edited\")");
        BlockProgram reopened = BlockProgram.open(edited);
        assertTrue(reopened.recovered);
        assertEquals(edited, reopened.original);
        assertEquals(0, reopened.blocks.length());
        assertTrue(reopened.render().endsWith(edited));
        assertTrue(BlockProgram.open("-- LuaC-blocks-v1:broken\nreturn 1\n").recovered);
    }
    @Test public void validatesValuesAndRejectsUnknownBlocks() throws Exception {
        assertTrue(BlockProgram.validName("score_1"));
        assertFalse(BlockProgram.validName("end"));
        assertFalse(BlockProgram.validName("_luac_block_i"));
        assertFalse(BlockProgram.validName("x;print(1)"));
        assertTrue(BlockProgram.validNumber("-2.5e+2"));
        assertFalse(BlockProgram.validNumber("1e9999"));
        assertFalse(BlockProgram.validNumber("1;print(1)"));
        BlockProgram program = BlockProgram.open("");
        program.blocks.put(new JSONObject().put("type", "repeat").put("value", "10001").put("children", new JSONArray()));
        try { program.render(); fail("repeat limit"); } catch (IllegalArgumentException expected) { }
    }
}
