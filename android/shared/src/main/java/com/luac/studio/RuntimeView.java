package com.luac.studio;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;
import java.util.HashMap;
import java.util.HashSet;
import org.json.JSONArray;
import org.json.JSONObject;

final class RuntimeView extends View {
    interface TouchSink { void touch(int id, double x, double y, boolean down); }

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final File project;
    private final TouchSink touchSink;
    private final HashMap<String, Bitmap> textures = new HashMap<>();
    private final HashSet<String> missingTextures = new HashSet<>();
    private JSONObject frame;
    private double cameraX, cameraY, zoom = 1;

    RuntimeView(Context context, File project, TouchSink touchSink) {
        super(context);
        this.project = project;
        this.touchSink = touchSink;
        setBackgroundColor(Color.rgb(7, 13, 20));
        setFocusable(true);
    }

    void setFrame(JSONObject value) {
        frame = value;
        if (value != null) {
            JSONObject camera = value.optJSONObject("camera");
            if (camera != null) {
                cameraX = camera.optDouble("x");
                cameraY = camera.optDouble("y");
                zoom = Math.max(0.1, Math.min(10, camera.optDouble("zoom", 1)));
            }
        }
        invalidate();
    }

    private float screenX(double x) { return (float) (getWidth() * 0.5 + (x - cameraX) * zoom); }
    private float screenY(double y) { return (float) (getHeight() * 0.5 + (y - cameraY) * zoom); }
    private boolean loveMode() { return frame != null && frame.optJSONObject("love") != null; }
    private double worldX(float x) { return loveMode() && loveScale() > 0 ? (x - loveLeft()) / loveScale() : (x - getWidth() * 0.5) / zoom + cameraX; }
    private double worldY(float y) { return loveMode() && loveScale() > 0 ? (y - loveTop()) / loveScale() : (y - getHeight() * 0.5) / zoom + cameraY; }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        JSONObject love = frame == null ? null : frame.optJSONObject("love");
        if (love != null) { drawLove(canvas, love); return; }
        canvas.drawColor(Color.rgb(7, 13, 20));
        paint.setStrokeWidth(1);
        paint.setColor(Color.rgb(14, 29, 40));
        float grid = (float) (48 * zoom);
        if (grid >= 12) {
            float startX = screenX(Math.floor(cameraX / 48 - getWidth() / zoom / 96) * 48);
            float startY = screenY(Math.floor(cameraY / 48 - getHeight() / zoom / 96) * 48);
            for (float x = startX; x < getWidth(); x += grid) canvas.drawLine(x, 0, x, getHeight(), paint);
            for (float y = startY; y < getHeight(); y += grid) canvas.drawLine(0, y, getWidth(), y, paint);
        }
        if (frame == null) {
            paint.setColor(Color.LTGRAY); paint.setTextSize(18);
            canvas.drawText("Starting project…", 24, 42, paint);
            return;
        }
        JSONArray entities = frame.optJSONArray("entities");
        if (entities != null) for (int index = 0; index < entities.length(); ++index) {
            JSONObject entity = entities.optJSONObject(index);
            if (entity != null) drawEntity(canvas, entity);
        }
        JSONArray texts = frame.optJSONArray("texts");
        if (texts != null) for (int index = 0; index < texts.length(); ++index) {
            JSONObject text = texts.optJSONObject(index);
            if (text == null) continue;
            paint.setColor(Color.WHITE);
            paint.setTextSize((float) text.optDouble("size", 16));
            canvas.drawText(text.optString("text"), (float) text.optDouble("x"), (float) text.optDouble("y"), paint);
        }
    }

    private float loveScale() { return Math.min(getWidth() / 800f, getHeight() / 600f); }
    private float loveLeft() { return (getWidth() - 800 * loveScale()) / 2; }
    private float loveTop() { return (getHeight() - 600 * loveScale()) / 2; }
    private void drawLove(Canvas canvas, JSONObject love) {
        canvas.drawColor(Color.BLACK);
        float scale = loveScale();
        if (scale <= 0) return;
        canvas.save(); canvas.translate(loveLeft(), loveTop()); canvas.scale(scale, scale);
        canvas.clipRect(0, 0, 800, 600);
        paint.setStyle(Paint.Style.FILL); paint.setColor(color(love.optJSONArray("background")));
        canvas.drawRect(0, 0, 800, 600, paint);
        JSONArray commands = love.optJSONArray("commands");
        if (commands != null) for (int i = 0; i < commands.length(); ++i) {
            JSONObject item = commands.optJSONObject(i);
            if (item == null) continue;
            paint.setColor(color(item.optJSONArray("color")));
            paint.setStyle(item.optBoolean("line") ? Paint.Style.STROKE : Paint.Style.FILL);
            paint.setStrokeWidth(1);
            float x = (float) item.optDouble("x"), y = (float) item.optDouble("y");
            float w = (float) item.optDouble("w"), h = (float) item.optDouble("h");
            switch (item.optInt("kind")) {
                case 1: canvas.drawRect(x, y, x + w, y + h, paint); break;
                case 2: if (w >= 0) canvas.drawCircle(x, y, w, paint); break;
                case 3: paint.setStyle(Paint.Style.FILL); paint.setTextSize(16);
                    canvas.drawText(item.optString("text"), x, y - paint.ascent(), paint); break;
                case 4:
                    Bitmap image = texture(item.optString("text"));
                    if (image != null) {
                        canvas.save(); canvas.translate(x, y);
                        canvas.rotate((float) Math.toDegrees(item.optDouble("rotation")));
                        canvas.scale((float) item.optDouble("sx", 1), (float) item.optDouble("sy", 1));
                        paint.setColorFilter(new PorterDuffColorFilter(paint.getColor(), PorterDuff.Mode.MULTIPLY));
                        paint.setAlpha(Color.alpha(color(item.optJSONArray("color"))));
                        canvas.drawBitmap(image, -(float) item.optDouble("ox"), -(float) item.optDouble("oy"), paint);
                        paint.setColorFilter(null); paint.setAlpha(255); canvas.restore();
                    }
                    break;
                default: break;
            }
        }
        paint.setStyle(Paint.Style.FILL); canvas.restore();
    }

    private void drawEntity(Canvas canvas, JSONObject entity) {
        double scale = entity.optDouble("scale", 1) * zoom;
        float x = screenX(entity.optDouble("x"));
        float y = screenY(entity.optDouble("y"));
        float halfWidth = (float) (entity.optDouble("width", 32) * scale * 0.5);
        float halfHeight = (float) (entity.optDouble("height", 32) * scale * 0.5);
        RectF rectangle = new RectF(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight);
        canvas.save();
        canvas.rotate((float) entity.optDouble("rotation"), x, y);
        String path = entity.optString("texture", "");
        Bitmap bitmap = path.isEmpty() || path.equals("null") ? null : texture(path);
        if (bitmap != null) canvas.drawBitmap(bitmap, null, rectangle, paint);
        else {
            int color = color(entity.optJSONArray("color"));
            paint.setColor(color);
            canvas.drawRoundRect(rectangle, Math.min(12, halfWidth * .2f), Math.min(12, halfHeight * .2f), paint);
            paint.setColor(Color.argb(110, 255, 255, 255));
            canvas.drawRect(rectangle.left + 3, rectangle.top + 3, rectangle.right - 3, rectangle.top + 6, paint);
        }
        canvas.restore();
    }

    private Bitmap texture(String path) {
        if (textures.containsKey(path)) return textures.get(path);
        if (missingTextures.contains(path)) return null;
        try {
            File file = new File(path);
            if (!file.isAbsolute()) file = new File(project, path);
            Bitmap bitmap = BitmapFactory.decodeFile(file.getCanonicalPath());
            if (bitmap == null) missingTextures.add(path);
            else textures.put(path, bitmap);
            return bitmap;
        } catch (Exception error) {
            missingTextures.add(path);
            return null;
        }
    }

    private static int color(JSONArray values) {
        if (values == null || values.length() < 3) return Color.rgb(65, 215, 170);
        int red = channel(values.optDouble(0, 1));
        int green = channel(values.optDouble(1, 1));
        int blue = channel(values.optDouble(2, 1));
        int alpha = channel(values.optDouble(3, 1));
        return Color.argb(alpha, red, green, blue);
    }
    private static int channel(double value) { return Math.max(0, Math.min(255, (int) Math.round(value * 255))); }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN || action == MotionEvent.ACTION_MOVE) {
            int start = action == MotionEvent.ACTION_MOVE ? 0 : index;
            int end = action == MotionEvent.ACTION_MOVE ? event.getPointerCount() : index + 1;
            for (int pointer = start; pointer < end; ++pointer)
                touchSink.touch(event.getPointerId(pointer), worldX(event.getX(pointer)), worldY(event.getY(pointer)), true);
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP || action == MotionEvent.ACTION_CANCEL) {
            touchSink.touch(event.getPointerId(index), worldX(event.getX(index)), worldY(event.getY(index)), false);
        }
        return true;
    }

    void dispose() {
        for (Bitmap bitmap : textures.values()) if (!bitmap.isRecycled()) bitmap.recycle();
        textures.clear();
    }
}
