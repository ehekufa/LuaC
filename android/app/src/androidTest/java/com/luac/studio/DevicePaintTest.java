package com.luac.studio;

import static org.junit.Assert.*;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.view.MotionEvent;
import android.widget.FrameLayout;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.ByteArrayOutputStream;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class DevicePaintTest {
    @Test public void drawingErasingUndoAndPngTransparency() throws Exception {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            try {
                FrameLayout parent = new FrameLayout(context);
                PaintView view = new PaintView(context);
                parent.addView(view, new FrameLayout.LayoutParams(512, 512));
                int spec = android.view.View.MeasureSpec.makeMeasureSpec(512, android.view.View.MeasureSpec.EXACTLY);
                parent.measure(spec, spec); parent.layout(0, 0, 512, 512);
                Bitmap screen = Bitmap.createBitmap(512, 512, Bitmap.Config.ARGB_8888);
                parent.draw(new Canvas(screen));
                view.setBrushColor(Color.RED);
                stroke(view);
                assertTrue(view.hasUnsavedChanges());
                assertEquals(Color.RED, pixel(view));
                view.setEraser(); stroke(view);
                assertEquals(0, Color.alpha(pixel(view)));
                view.undo();
                assertEquals(Color.RED, pixel(view));
                view.markSaved(); assertFalse(view.hasUnsavedChanges());
                view.clear(); assertEquals(0, Color.alpha(pixel(view)));
                view.undo(); assertEquals(Color.RED, pixel(view));
                screen.recycle();
            } catch (Exception e) { throw new AssertionError(e); }
        });
    }
    private static void stroke(PaintView view) {
        long time = android.os.SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(time, time, MotionEvent.ACTION_DOWN, 256, 256, 0);
        MotionEvent up = MotionEvent.obtain(time, time + 1, MotionEvent.ACTION_UP, 256, 256, 0);
        view.dispatchTouchEvent(down); view.dispatchTouchEvent(up); down.recycle(); up.recycle();
    }
    private static int pixel(PaintView view) throws Exception {
        ByteArrayOutputStream output = new ByteArrayOutputStream(); view.writePng(output);
        byte[] png = output.toByteArray();
        Bitmap decoded = BitmapFactory.decodeByteArray(png, 0, png.length);
        assertNotNull(decoded); assertEquals(512, decoded.getWidth()); assertEquals(512, decoded.getHeight());
        int result = decoded.getPixel(256, 256); decoded.recycle(); return result;
    }
}
