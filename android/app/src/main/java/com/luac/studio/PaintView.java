package com.luac.studio;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import java.io.IOException;
import java.io.OutputStream;

/** A fixed-resolution transparent image, scaled to fit the available editor space. */
final class PaintView extends View {
    static final int SIZE = 512;
    private final Bitmap image = Bitmap.createBitmap(SIZE, SIZE, Bitmap.Config.ARGB_8888);
    private Bitmap undo;
    private final Canvas imageCanvas = new Canvas(image);
    private final Paint brush = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint display = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint checker = new Paint();
    private final RectF destination = new RectF();
    private int color = Color.BLACK;
    private boolean eraser, dirty, drawing;
    private float lastX, lastY;

    PaintView(Context context) {
        super(context);
        setContentDescription("Drawing canvas");
        brush.setColor(color);
        brush.setStrokeCap(Paint.Cap.ROUND);
        brush.setStrokeJoin(Paint.Join.ROUND);
        brush.setStyle(Paint.Style.STROKE);
        brush.setStrokeWidth(8);
    }

    void setBrushColor(int color) { this.color = color; eraser = false; updateBrush(); }
    void setEraser() { eraser = true; updateBrush(); }
    void setBrushSize(float size) { brush.setStrokeWidth(size); }
    private void updateBrush() {
        brush.setColor(color);
        brush.setXfermode(eraser ? new PorterDuffXfermode(PorterDuff.Mode.CLEAR) : null);
    }
    boolean hasUnsavedChanges() { return dirty; }
    void markSaved() { dirty = false; }
    void clear() { snapshot(); image.eraseColor(Color.TRANSPARENT); dirty = true; invalidate(); }
    void undo() {
        if (undo == null) return;
        imageCanvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR);
        imageCanvas.drawBitmap(undo, 0, 0, display);
        undo.recycle(); undo = null; dirty = true; invalidate();
    }
    private void snapshot() {
        if (undo != null) undo.recycle();
        undo = image.copy(Bitmap.Config.ARGB_8888, false);
    }
    void writePng(OutputStream output) throws IOException {
        if (!image.compress(Bitmap.CompressFormat.PNG, 100, output)) throw new IOException("Cannot encode PNG");
    }
    void restore(String path, boolean wasDirty) throws IOException {
        Bitmap saved = BitmapFactory.decodeFile(path);
        if (saved == null || saved.getWidth() != SIZE || saved.getHeight() != SIZE)
            throw new IOException("Cannot restore drawing");
        imageCanvas.drawBitmap(saved, 0, 0, display);
        saved.recycle(); dirty = wasDirty; invalidate();
    }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float scale = Math.min(getWidth(), getHeight()) / (float) SIZE;
        if (scale <= 0) return;
        float width = SIZE * scale, left = (getWidth() - width) / 2, top = (getHeight() - width) / 2;
        destination.set(left, top, left + width, top + width);
        // Checkerboard is only a preview; transparent pixels remain transparent in the PNG.
        int tiles = 16;
        for (int y = 0; y < tiles; y++) for (int x = 0; x < tiles; x++) {
            checker.setColor((x + y) % 2 == 0 ? 0xffd8d8d8 : 0xffaaaaaa);
            float step = width / tiles;
            canvas.drawRect(left + x * step, top + y * step, left + (x + 1) * step, top + (y + 1) * step, checker);
        }
        canvas.drawBitmap(image, null, destination, display);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (destination.width() <= 0) return false;
        int action = event.getActionMasked();
        float x = Math.max(0, Math.min(SIZE - 1, (event.getX() - destination.left) * SIZE / destination.width()));
        float y = Math.max(0, Math.min(SIZE - 1, (event.getY() - destination.top) * SIZE / destination.height()));
        if (action == MotionEvent.ACTION_DOWN) {
            if (!destination.contains(event.getX(), event.getY())) return false;
            getParent().requestDisallowInterceptTouchEvent(true);
            snapshot(); drawing = true; dirty = true; lastX = x; lastY = y;
            imageCanvas.drawPoint(x, y, brush); invalidate(); return true;
        }
        if (!drawing) return false;
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_UP) {
            for (int i = 0; i < event.getHistorySize(); i++) {
                float hx = Math.max(0, Math.min(SIZE - 1, (event.getHistoricalX(i) - destination.left) * SIZE / destination.width()));
                float hy = Math.max(0, Math.min(SIZE - 1, (event.getHistoricalY(i) - destination.top) * SIZE / destination.height()));
                imageCanvas.drawLine(lastX, lastY, hx, hy, brush); lastX = hx; lastY = hy;
            }
            imageCanvas.drawLine(lastX, lastY, x, y, brush); lastX = x; lastY = y;
            invalidate();
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            drawing = false; getParent().requestDisallowInterceptTouchEvent(false);
            if (action == MotionEvent.ACTION_UP) performClick();
        }
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
}
