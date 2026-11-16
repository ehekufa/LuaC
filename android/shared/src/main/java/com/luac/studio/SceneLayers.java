package com.luac.studio;

import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

/** Stable ordering preserves the existing order for entities on the same layer. */
final class SceneLayers {
    static List<JSONObject> ordered(JSONArray input) {
        List<JSONObject> result = new ArrayList<>();
        if (input != null) for (int i = 0; i < input.length(); i++) {
            JSONObject entity = input.optJSONObject(i);
            if (entity != null) result.add(entity);
        }
        result.sort((a, b) -> Double.compare(a.optDouble("z", 0), b.optDouble("z", 0)));
        return result;
    }
    private SceneLayers() { }
}
