package com.luac.studio;

import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.HashSet;
import java.util.Set;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** A block document is embedded in a Lua comment, so project archives carry it too. */
final class BlockProgram {
    private static final String HEADER = "-- LuaC-blocks-v1:";
    private static final Set<String> KEYWORDS = new HashSet<>(Arrays.asList(
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
        "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"));
    final String original;
    final JSONArray blocks;
    boolean dirty;
    final boolean recovered;

    private BlockProgram(String original, JSONArray blocks, boolean recovered) {
        this.original = original; this.blocks = blocks; this.recovered = recovered;
    }

    static BlockProgram open(String source) {
        if (source.startsWith(HEADER)) {
            try {
                int end = source.indexOf('\n');
                if (end < 0) throw new IllegalArgumentException();
                String json = new String(Base64.getDecoder().decode(source.substring(HEADER.length(), end)), StandardCharsets.UTF_8);
                JSONObject data = new JSONObject(json);
                if (data.getInt("version") != 1) throw new IllegalArgumentException();
                BlockProgram result = new BlockProgram(data.getString("original"), data.getJSONArray("blocks"), false);
                if ((source.substring(0, end + 1) + result.body()).equals(source)) return result;
            } catch (Exception ignored) { }
            // Do not trust metadata if the user edited the generated Lua or the metadata is damaged.
            return new BlockProgram(source, new JSONArray(), true);
        }
        return new BlockProgram(source, new JSONArray(), false);
    }

    String render() throws JSONException {
        JSONObject document = new JSONObject();
        document.put("version", 1); document.put("original", original); document.put("blocks", blocks);
        String encoded = Base64.getEncoder().encodeToString(document.toString().getBytes(StandardCharsets.UTF_8));
        return HEADER + encoded + "\n" + body();
    }

    private String body() throws JSONException {
        StringBuilder lua = new StringBuilder();
        int[] count = {0};
        generate(lua, blocks, 0, count);
        lua.append("-- Existing script follows\n").append(original);
        return lua.toString();
    }

    private static void generate(StringBuilder out, JSONArray list, int depth, int[] count) throws JSONException {
        if ((depth > 8 && list.length() > 0) || list.length() > 200) throw new IllegalArgumentException("Too many nested blocks");
        for (int i = 0; i < list.length(); i++) {
            if (++count[0] > 200) throw new IllegalArgumentException("Too many blocks");
            JSONObject block = list.getJSONObject(i);
            String type = block.getString("type");
            for (int n = 0; n < depth; n++) out.append("  ");
            switch (type) {
                case "scene": out.append("world = engine.world()\n"); break;
                case "object": case "position": case "move": case "size": case "velocity": {
                    String name = block.getString("name"), x = block.getString("x"), y = block.getString("y");
                    if (!validName(name) || name.equals("world") || name.equals("engine") || !validNumber(x) || !validNumber(y))
                        throw new IllegalArgumentException("Invalid object or coordinates");
                    if (type.equals("object")) {
                        out.append("world = world or engine.world()\n");
                        for (int n = 0; n < depth; n++) out.append("  ");
                        out.append(name).append(" = engine.create(world, ").append(quote(name));
                    } else {
                        String method = type.equals("position") ? "set_position" : type.equals("move") ? "translate" :
                            type.equals("size") ? "set_size" : "set_velocity";
                        out.append("engine.").append(method).append('(').append(name);
                    }
                    out.append(", ").append(x).append(", ").append(y).append(")\n"); break;
                }
                case "print": out.append("print(").append(quote(block.getString("text"))).append(")\n"); break;
                case "printvar": {
                    String name = block.getString("name");
                    if (!validName(name)) throw new IllegalArgumentException("Invalid variable");
                    out.append("print(").append(name).append(")\n"); break;
                }
                case "textvar": {
                    String name = block.getString("name");
                    if (!validName(name)) throw new IllegalArgumentException("Invalid variable");
                    out.append(name).append(" = ").append(quote(block.getString("text"))).append('\n'); break;
                }
                case "set": case "change": case "multiply": case "divide": {
                    String name = block.getString("name"), value = block.getString("value");
                    if (!validName(name) || !validNumber(value) || (type.equals("divide") && Double.parseDouble(value) == 0))
                        throw new IllegalArgumentException("Invalid variable or number");
                    out.append(name).append(" = ");
                    if (type.equals("change")) out.append('(').append(name).append(" or 0) + ");
                    if (type.equals("multiply")) out.append('(').append(name).append(" or 0) * ");
                    if (type.equals("divide")) out.append('(').append(name).append(" or 0) / ");
                    out.append(value).append('\n'); break;
                }
                case "repeat": {
                    String times = block.getString("value");
                    if (!times.matches("[0-9]{1,5}") || Integer.parseInt(times) > 10000) throw new IllegalArgumentException("Invalid repeat count");
                    out.append("for _luac_block_i = 1, ").append(times).append(" do\n");
                    generate(out, block.getJSONArray("children"), depth + 1, count);
                    for (int n = 0; n < depth; n++) out.append("  ");
                    out.append("end\n"); break;
                }
                case "if": case "ifelse": {
                    String name = block.getString("name"), value = block.getString("value"), op = block.getString("op");
                    if (!validName(name) || !validNumber(value) || !Arrays.asList("==", "~=", "<", ">", "<=", ">=").contains(op))
                        throw new IllegalArgumentException("Invalid condition");
                    out.append("if ").append(name).append(' ').append(op).append(' ').append(value).append(" then\n");
                    generate(out, block.getJSONArray("children"), depth + 1, count);
                    if (type.equals("ifelse")) {
                        for (int n = 0; n < depth; n++) out.append("  ");
                        out.append("else\n");
                        generate(out, block.getJSONArray("otherwise"), depth + 1, count);
                    }
                    for (int n = 0; n < depth; n++) out.append("  ");
                    out.append("end\n"); break;
                }
                default: throw new IllegalArgumentException("Unknown block");
            }
        }
    }

    static boolean validName(String name) { return name.matches("[A-Za-z_][A-Za-z_0-9]*") && !name.startsWith("_luac_block_") && !KEYWORDS.contains(name); }
    static boolean validNumber(String number) {
        if (!number.matches("-?(?:[0-9]+(?:\\.[0-9]+)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?")) return false;
        try { return Double.isFinite(Double.parseDouble(number)); } catch (NumberFormatException e) { return false; }
    }
    private static String quote(String value) {
        StringBuilder result = new StringBuilder("\"");
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (c == '\\' || c == '"') result.append('\\').append(c);
            else if (c < 32 || c == 127) result.append(String.format(java.util.Locale.ROOT, "\\%03d", (int)c));
            else result.append(c);
        }
        return result.append('"').toString();
    }
}
