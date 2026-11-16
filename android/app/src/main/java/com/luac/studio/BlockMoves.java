package com.luac.studio;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Moves an existing subtree without copying it or allowing cycles. */
final class BlockMoves {
    private BlockMoves() { }

    static boolean move(JSONArray from, int index, JSONArray to, int slot, int targetDepth) throws JSONException {
        if (index < 0 || index >= from.length() || slot < 0 || slot > to.length() || targetDepth < 0)
            return false;
        JSONObject block = from.getJSONObject(index);
        if (containsList(block, to) || targetDepth + height(block) > 8) return false;
        if (from == to && (slot == index || slot == index + 1)) return false;
        from.remove(index);
        if (from == to && slot > index) slot--;
        int size = to.length();
        to.put(JSONObject.NULL);
        for (int i = size; i > slot; i--) to.put(i, to.get(i - 1));
        to.put(slot, block);
        return true;
    }

    private static boolean containsList(JSONObject block, JSONArray target) throws JSONException {
        for (String key : new String[] {"children", "otherwise"}) {
            if (!block.has(key)) continue;
            JSONArray children = block.getJSONArray(key);
            if (children == target) return true;
            for (int i = 0; i < children.length(); i++)
                if (containsList(children.getJSONObject(i), target)) return true;
        }
        return false;
    }

    private static int height(JSONObject block) throws JSONException {
        int height = 0;
        for (String key : new String[] {"children", "otherwise"}) {
            if (!block.has(key)) continue;
            JSONArray children = block.getJSONArray(key);
            for (int i = 0; i < children.length(); i++)
                height = Math.max(height, 1 + height(children.getJSONObject(i)));
        }
        return height;
    }
}
