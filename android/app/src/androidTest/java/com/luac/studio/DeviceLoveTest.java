package com.luac.studio;

import static org.junit.Assert.*;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.io.FileOutputStream;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class DeviceLoveTest {
    @Test public void drawsAndMapsTouchToLoveCoordinates() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            try {
                double[] touch = new double[2];
                File project = new File(context.getCacheDir(), "love-test");
                assertTrue(project.isDirectory() || project.mkdirs());
                Bitmap source = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888);
                source.eraseColor(Color.WHITE);
                try (FileOutputStream output = new FileOutputStream(new File(project, "test.png"))) {
                    assertTrue(source.compress(Bitmap.CompressFormat.PNG, 100, output));
                }
                source.recycle();
                RuntimeView view = new RuntimeView(context, project,
                    (id, x, y, down) -> { touch[0] = x; touch[1] = y; });
                FrameLayout parent = new FrameLayout(context);
                parent.addView(view, new FrameLayout.LayoutParams(800, 800));
                int spec = View.MeasureSpec.makeMeasureSpec(800, View.MeasureSpec.EXACTLY);
                parent.measure(spec, spec); parent.layout(0, 0, 800, 800);
                view.setFrame(new JSONObject("{\"love\":{\"background\":[0,0,0,1],\"commands\":[{\"kind\":1,\"x\":40,\"y\":50,\"w\":80,\"h\":70,\"color\":[1,0,0,1]},{\"kind\":4,\"x\":200,\"y\":200,\"sx\":10,\"sy\":10,\"text\":\"test.png\",\"color\":[0,1,0,1]}]}}"));
                Bitmap bitmap = Bitmap.createBitmap(800, 800, Bitmap.Config.ARGB_8888);
                parent.draw(new Canvas(bitmap));
                assertEquals(Color.RED, bitmap.getPixel(60, 170));
                assertEquals(Color.GREEN, bitmap.getPixel(205, 305));
                long time = android.os.SystemClock.uptimeMillis();
                MotionEvent event = MotionEvent.obtain(time, time, MotionEvent.ACTION_DOWN, 60, 170, 0);
                view.dispatchTouchEvent(event); event.recycle();
                assertEquals(60, touch[0], 0.01); assertEquals(70, touch[1], 0.01);
                bitmap.recycle(); view.dispose();
            } catch (Exception e) { throw new AssertionError(e); }
        });
    }
}
