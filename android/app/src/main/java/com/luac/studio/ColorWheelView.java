package com.luac.studio;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.SweepGradient;
import android.view.MotionEvent;
import android.view.View;

/** Hue ring and saturation/value square, shared by theme and Paint. */
final class ColorWheelView extends View {
    interface Listener { void onColorChanged(int color); }
    private final float[] hsv = {0, 1, 1};
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF square = new RectF();
    private Listener listener;
    private float cx, cy, radius, inner;
    private int drag;

    ColorWheelView(Context context, int color) {
        super(context);
        setContentDescription("Color wheel: hue ring and saturation square");
        setFocusable(true);
        setColor(color);
    }
    int getColor() { return Color.HSVToColor(hsv); }
    void setColor(int color) { Color.colorToHSV(color, hsv); invalidate(); }
    void setListener(Listener listener) { this.listener = listener; }
    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int target = Math.round(260 * getResources().getDisplayMetrics().density);
        int width = resolveSize(target, widthSpec);
        int height = resolveSize(width, heightSpec);
        setMeasuredDimension(width, height);
    }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        cx = getWidth() / 2f; cy = getHeight() / 2f;
        radius = Math.min(getWidth(), getHeight()) * .46f;
        inner = radius * .76f;
        float half = inner * .65f;
        square.set(cx - half, cy - half, cx + half, cy + half);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(radius - inner);
        paint.setShader(new SweepGradient(cx, cy, new int[] {Color.RED, Color.YELLOW, Color.GREEN,
                Color.CYAN, Color.BLUE, Color.MAGENTA, Color.RED}, null));
        canvas.drawCircle(cx, cy, (radius + inner) / 2, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
        int cells = 24;
        float[] color = {hsv[0], 0, 0};
        for (int y = 0; y < cells; y++) for (int x = 0; x < cells; x++) {
            color[1] = (x + .5f) / cells; color[2] = 1 - (y + .5f) / cells;
            paint.setColor(Color.HSVToColor(color));
            canvas.drawRect(square.left + square.width() * x / cells, square.top + square.height() * y / cells,
                    square.left + square.width() * (x + 1) / cells + 1,
                    square.top + square.height() * (y + 1) / cells + 1, paint);
        }
        paint.setColor(Color.WHITE); paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(Math.max(2, getResources().getDisplayMetrics().density * 2));
        float angle = (float) Math.toRadians(hsv[0]), r = (radius + inner) / 2;
        canvas.drawCircle(cx + r * (float) Math.cos(angle), cy + r * (float) Math.sin(angle), 6, paint);
        canvas.drawCircle(square.left + hsv[1] * square.width(), square.top + (1 - hsv[2]) * square.height(), 6, paint);
        paint.setStyle(Paint.Style.FILL);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        float x = event.getX(), y = event.getY();
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            float distance = (float) Math.hypot(x - cx, y - cy);
            drag = distance >= inner && distance <= radius ? 1 : square.contains(x, y) ? 2 : 0;
            if (drag == 0) return false;
            getParent().requestDisallowInterceptTouchEvent(true);
        }
        if (drag == 0) return false;
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN || event.getActionMasked() == MotionEvent.ACTION_MOVE || event.getActionMasked() == MotionEvent.ACTION_UP) {
            if (drag == 1) {
                hsv[0] = (float) Math.toDegrees(Math.atan2(y - cy, x - cx));
                if (hsv[0] < 0) hsv[0] += 360;
            } else {
                hsv[1] = Math.max(0, Math.min(1, (x - square.left) / square.width()));
                hsv[2] = Math.max(0, Math.min(1, 1 - (y - square.top) / square.height()));
            }
            invalidate();
            if (listener != null) listener.onColorChanged(getColor());
        }
        if (event.getActionMasked() == MotionEvent.ACTION_UP || event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
            drag = 0; getParent().requestDisallowInterceptTouchEvent(false);
            if (event.getActionMasked() == MotionEvent.ACTION_UP) performClick();
        }
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
}
