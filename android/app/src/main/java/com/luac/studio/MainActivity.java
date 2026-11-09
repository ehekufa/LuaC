package com.luac.studio;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Random;

public final class MainActivity extends Activity {
    private static boolean nativeAvailable;
    private static String nativeLoadError = "";
    static {
        try {
            System.loadLibrary("luac_native");
            nativeAvailable = true;
        } catch (Throwable error) {
            nativeAvailable = false;
            nativeLoadError = error.getClass().getSimpleName();
        }
    }

    private EditText editor;
    private TextView output;
    private SnakeView snake;
    private boolean snakeVisible;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private static native String nativeCheck(String source);
    private static native String nativeGenerate(String source);
    private static native String nativeVersion();
    private static native boolean nativeVulkanCreate(Surface surface);
    private static native void nativeVulkanDraw();
    private static native void nativeVulkanDestroy();

    private final String demo =
        "-- LuaC Studio for Android\n" +
        "local function greet(name)\n" +
        "  return 'Hello, ' .. name\n" +
        "end\n\n" +
        "print(greet('Vulkan'))\n";

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        showSnake();
    }

    private Button button(String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(listener);
        return button;
    }

    private void showSnake() {
        snakeVisible = true;
        int background = Color.rgb(9, 13, 22);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(background);
        root.setPadding(18, 12, 18, 12);

        TextView title = new TextView(this);
        title.setText("LuaC Snake");
        title.setTextColor(Color.rgb(66, 221, 176));
        title.setTextSize(26);
        title.setGravity(android.view.Gravity.CENTER);
        root.addView(title);

        TextView hint = new TextView(this);
        hint.setText("Свайп или кнопки • собирай яблоки • после проигрыша коснись поля");
        hint.setTextColor(Color.rgb(180, 195, 212));
        hint.setGravity(android.view.Gravity.CENTER);
        root.addView(hint);

        snake = new SnakeView(this);
        root.addView(snake, new LinearLayout.LayoutParams(-1, 0, 1.0f));

        LinearLayout directions = new LinearLayout(this);
        directions.setOrientation(LinearLayout.HORIZONTAL);
        directions.addView(button("▲", v -> snake.turn(0, -1)), new LinearLayout.LayoutParams(0, -2, 1));
        directions.addView(button("◀", v -> snake.turn(-1, 0)), new LinearLayout.LayoutParams(0, -2, 1));
        directions.addView(button("▼", v -> snake.turn(0, 1)), new LinearLayout.LayoutParams(0, -2, 1));
        directions.addView(button("▶", v -> snake.turn(1, 0)), new LinearLayout.LayoutParams(0, -2, 1));
        root.addView(directions);
        root.addView(button("LUAC EDITOR", v -> showEditor()));
        setContentView(root);
        snake.start();
    }

    private String safeCompilerCall(String source, boolean generate) {
        if (!nativeAvailable) return "Native library is unavailable: " + nativeLoadError +
            "\nSnake still works; rebuild the APK for this device ABI.";
        try {
            return generate ? nativeGenerate(source) : nativeCheck(source);
        } catch (Throwable error) {
            return "Compiler error: " + error;
        }
    }

    private void safeVulkan(SurfaceView view, int action) {
        if (!nativeAvailable) return;
        try {
            if (action == 0) nativeVulkanCreate(view.getHolder().getSurface());
            else if (action == 1) nativeVulkanDraw();
            else nativeVulkanDestroy();
        } catch (Throwable error) {
            if (output != null) output.setText("Vulkan is unavailable on this device: " + error +
                "\nThe editor and Snake remain available.");
        }
    }

    private void showEditor() {
        snakeVisible = false;
        handler.removeCallbacksAndMessages(null);
        int background = Color.rgb(9, 13, 22);
        int accent = Color.rgb(66, 221, 176);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(background);
        root.setPadding(18, 12, 18, 12);

        SurfaceView vulkan = new SurfaceView(this);
        root.addView(vulkan, new LinearLayout.LayoutParams(-1, 170));
        vulkan.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) { safeVulkan(vulkan, 0); }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { safeVulkan(vulkan, 1); }
            @Override public void surfaceDestroyed(SurfaceHolder holder) { safeVulkan(vulkan, 2); }
        });

        TextView title = new TextView(this);
        String version = "native unavailable";
        if (nativeAvailable) {
            try { version = nativeVersion(); } catch (Throwable error) { version = error.toString(); }
        }
        title.setText("LuaC Studio  •  " + version + "  •  Vulkan");
        title.setTextColor(accent);
        title.setTextSize(18);
        title.setPadding(0, 12, 0, 8);
        root.addView(title);

        editor = new EditText(this);
        editor.setText(demo);
        editor.setTextColor(Color.rgb(220, 230, 241));
        editor.setHintTextColor(Color.GRAY);
        editor.setBackgroundColor(Color.rgb(14, 21, 34));
        editor.setPadding(18, 12, 18, 12);
        editor.setGravity(android.view.Gravity.TOP);
        editor.setMinLines(9);
        root.addView(editor, new LinearLayout.LayoutParams(-1, 0, 1.0f));

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        Button check = button("CHECK", v -> output.setText(safeCompilerCall(editor.getText().toString(), false)));
        Button generate = button("GENERATE C", v -> output.setText(safeCompilerCall(editor.getText().toString(), true)));
        Button reset = button("DEMO", v -> editor.setText(demo));
        Button back = button("SNAKE", v -> showSnake());
        buttons.addView(check, new LinearLayout.LayoutParams(0, -2, 1));
        buttons.addView(generate, new LinearLayout.LayoutParams(0, -2, 1));
        buttons.addView(reset, new LinearLayout.LayoutParams(0, -2, 1));
        buttons.addView(back, new LinearLayout.LayoutParams(0, -2, 1));
        root.addView(buttons);

        ScrollView scroll = new ScrollView(this);
        output = new TextView(this);
        output.setTextIsSelectable(true);
        output.setTextColor(Color.rgb(180, 195, 212));
        output.setTextSize(12);
        output.setText("Syntax check and native C generation run inside the APK.\n" +
            "JIT/AOT execution requires a host C toolchain; use the desktop build to run programs.");
        scroll.addView(output);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 230));
        setContentView(root);
    }

    @Override public void onBackPressed() {
        if (!snakeVisible) showSnake();
        else super.onBackPressed();
    }

    @Override protected void onPause() {
        handler.removeCallbacksAndMessages(null);
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        if (snakeVisible && snake != null) snake.start();
    }

    @Override protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        if (nativeAvailable) {
            try { nativeVulkanDestroy(); } catch (Throwable ignored) { }
        }
        super.onDestroy();
    }

    private final class SnakeView extends View {
        private static final int COLS = 20;
        private static final int ROWS = 20;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final ArrayList<Cell> cells = new ArrayList<>();
        private final Random random = new Random();
        private Cell food;
        private int dx = 1, dy = 0;
        private int queuedX = 1, queuedY = 0;
        private boolean gameOver;
        private int score;
        private float downX, downY;

        private final Runnable tick = new Runnable() {
            @Override public void run() {
                if (!snakeVisible) return;
                step();
                invalidate();
                handler.postDelayed(this, gameOver ? 250 : Math.max(65, 145 - score * 2));
            }
        };

        SnakeView(Context context) {
            super(context);
            paint.setTypeface(android.graphics.Typeface.MONOSPACE);
            reset();
        }

        void start() {
            handler.removeCallbacks(tick);
            handler.post(tick);
        }

        private void reset() {
            cells.clear();
            cells.add(new Cell(10, 10));
            cells.add(new Cell(9, 10));
            cells.add(new Cell(8, 10));
            dx = queuedX = 1;
            dy = queuedY = 0;
            score = 0;
            gameOver = false;
            placeFood();
        }

        void turn(int x, int y) {
            if (gameOver) { reset(); return; }
            if (x != -dx || y != -dy) { queuedX = x; queuedY = y; }
        }

        private void placeFood() {
            if (cells.size() >= COLS * ROWS) return;
            do { food = new Cell(random.nextInt(COLS), random.nextInt(ROWS)); }
            while (cells.contains(food));
        }

        private void step() {
            if (gameOver) return;
            dx = queuedX; dy = queuedY;
            Cell head = cells.get(0);
            Cell next = new Cell(head.x + dx, head.y + dy);
            if (next.x < 0 || next.x >= COLS || next.y < 0 || next.y >= ROWS) {
                gameOver = true;
                return;
            }
            boolean eating = next.equals(food);
            int limit = eating ? cells.size() : cells.size() - 1;
            for (int i = 0; i < limit; ++i) {
                if (next.equals(cells.get(i))) { gameOver = true; return; }
            }
            cells.add(0, next);
            if (eating) { score += 10; placeFood(); }
            else cells.remove(cells.size() - 1);
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                downX = event.getX(); downY = event.getY();
                return true;
            }
            if (event.getAction() == MotionEvent.ACTION_UP) {
                float x = event.getX() - downX;
                float y = event.getY() - downY;
                if (gameOver) reset();
                else if (Math.abs(x) > 35 || Math.abs(y) > 35) {
                    if (Math.abs(x) > Math.abs(y)) turn(x > 0 ? 1 : -1, 0);
                    else turn(0, y > 0 ? 1 : -1);
                }
                performClick();
                return true;
            }
            return true;
        }

        @Override public boolean performClick() {
            super.performClick();
            return true;
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.drawColor(Color.rgb(8, 15, 24));
            float cellSize = Math.min(getWidth() / (float) COLS, getHeight() / (float) ROWS);
            float offsetX = (getWidth() - cellSize * COLS) / 2;
            float offsetY = (getHeight() - cellSize * ROWS) / 2;
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(Math.max(28, cellSize * 0.75f));
            paint.setColor(Color.rgb(190, 210, 222));
            canvas.drawText("SCORE  " + score, getWidth() / 2f, Math.max(34, offsetY - 10), paint);
            paint.setColor(Color.rgb(15, 30, 41));
            for (int i = 0; i <= COLS; ++i) {
                float x = offsetX + i * cellSize;
                canvas.drawLine(x, offsetY, x, offsetY + ROWS * cellSize, paint);
            }
            for (int i = 0; i <= ROWS; ++i) {
                float y = offsetY + i * cellSize;
                canvas.drawLine(offsetX, y, offsetX + COLS * cellSize, y, paint);
            }
            drawCell(canvas, food, offsetX, offsetY, cellSize, Color.rgb(255, 100, 115), true);
            for (int i = cells.size() - 1; i >= 0; --i) {
                int color = i == 0 ? Color.rgb(100, 245, 190) : Color.rgb(45, 185, 135);
                drawCell(canvas, cells.get(i), offsetX, offsetY, cellSize, color, false);
            }
            if (gameOver) {
                paint.setColor(Color.argb(215, 5, 8, 14));
                canvas.drawRect(0, 0, getWidth(), getHeight(), paint);
                paint.setColor(Color.rgb(255, 115, 130));
                paint.setTextSize(Math.max(42, cellSize));
                canvas.drawText("GAME OVER", getWidth() / 2f, getHeight() / 2f, paint);
                paint.setColor(Color.WHITE);
                paint.setTextSize(Math.max(28, cellSize * 0.65f));
                canvas.drawText("Tap to restart", getWidth() / 2f, getHeight() / 2f + cellSize * 1.4f, paint);
            }
        }

        private void drawCell(Canvas canvas, Cell cell, float ox, float oy, float size, int color, boolean round) {
            float x = ox + cell.x * size;
            float y = oy + cell.y * size;
            float pad = Math.max(1, size * 0.07f);
            paint.setColor(color);
            canvas.drawRoundRect(x + pad, y + pad, x + size - pad, y + size - pad,
                size * (round ? 0.45f : 0.18f), size * (round ? 0.45f : 0.18f), paint);
        }
    }

    private static final class Cell {
        final int x, y;
        Cell(int x, int y) { this.x = x; this.y = y; }
        @Override public boolean equals(Object other) {
            return other instanceof Cell && ((Cell) other).x == x && ((Cell) other).y == y;
        }
        @Override public int hashCode() { return x * 31 + y; }
    }
}
