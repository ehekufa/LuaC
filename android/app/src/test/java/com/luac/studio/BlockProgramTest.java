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
    @Test public void moveBlocksAcrossListsAndRejectCycles() throws Exception {
        JSONObject a = new JSONObject().put("type", "print").put("text", "a");
        JSONObject b = new JSONObject().put("type", "print").put("text", "b");
        JSONArray nested = new JSONArray();
        JSONObject loop = new JSONObject().put("type", "repeat").put("value", "2").put("children", nested);
        JSONArray root = new JSONArray().put(a).put(loop).put(b);
        assertTrue(BlockMoves.move(root, 2, nested, 0, 1));
        assertEquals(2, root.length()); assertSame(b, nested.get(0));
        assertFalse(BlockMoves.move(root, 1, nested, 1, 1));
        assertSame(loop, root.get(1)); assertSame(b, nested.get(0));
        assertTrue(BlockMoves.move(nested, 0, root, 0, 0));
        assertSame(b, root.get(0)); assertSame(a, root.get(1));
        assertTrue(BlockMoves.move(root, 0, root, 3, 0));
        assertSame(b, root.get(2));
        assertFalse(BlockMoves.move(root, 2, root, 3, 0));
        assertFalse(BlockMoves.move(root, 1, new JSONArray(), 0, 9));
    }
    @Test public void generatesBranchesTextMathAndSceneBlocks() throws Exception {
        BlockProgram program = BlockProgram.open("-- game\n");
        program.blocks.put(new JSONObject().put("type", "scene"));
        program.blocks.put(new JSONObject().put("type", "object").put("name", "hero").put("x", "0").put("y", "2"));
        program.blocks.put(new JSONObject().put("type", "velocity").put("name", "hero").put("x", "10").put("y", "0"));
        program.blocks.put(new JSONObject().put("type", "set").put("name", "score").put("value", "6"));
        program.blocks.put(new JSONObject().put("type", "divide").put("name", "score").put("value", "2"));
        JSONArray yes = new JSONArray().put(new JSONObject().put("type", "textvar").put("name", "message").put("text", "yes"));
        JSONArray no = new JSONArray().put(new JSONObject().put("type", "textvar").put("name", "message").put("text", "no"));
        program.blocks.put(new JSONObject().put("type", "ifelse").put("name", "score").put("value", "3").put("op", "==")
            .put("children", yes).put("otherwise", no));
        program.blocks.put(new JSONObject().put("type", "printvar").put("name", "message"));
        String lua = program.render();
        assertTrue(lua.contains("world = engine.world()\nworld = world or engine.world()\nhero = engine.create(world, \"hero\", 0, 2)\n"));
        assertTrue(lua.contains("engine.set_velocity(hero, 10, 0)\n"));
        assertTrue(lua.contains("if score == 3 then\n  message = \"yes\"\nelse\n  message = \"no\"\nend\nprint(message)\n"));
        assertEquals(lua, BlockProgram.open(lua).render());
        program.blocks.put(new JSONObject().put("type", "divide").put("name", "score").put("value", "0"));
        try { program.render(); fail("zero division"); } catch (IllegalArgumentException expected) { }
    }
}
