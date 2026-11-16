package com.luac.studio;

import static org.junit.Assert.*;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Test;

public class SceneLayersTest {
    @Test public void backgroundIsBehindAndEqualLayersKeepTheirOrder() throws Exception {
        JSONObject first = new JSONObject().put("name", "first");
        JSONObject background = new JSONObject().put("z", -1);
        JSONObject second = new JSONObject().put("name", "second");
        List<JSONObject> ordered = SceneLayers.ordered(new JSONArray().put(first).put(background).put(second));
        assertSame(background, ordered.get(0));
        assertSame(first, ordered.get(1));
        assertSame(second, ordered.get(2));
        assertTrue(SceneLayers.ordered(null).isEmpty());
    }
}
