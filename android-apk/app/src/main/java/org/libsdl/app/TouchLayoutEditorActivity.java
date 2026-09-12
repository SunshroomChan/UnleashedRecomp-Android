package org.libsdl.app;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Standalone touch-layout editor that does not load SDL, Vulkan, or game data. */
public final class TouchLayoutEditorActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(new EditorView());
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) hideSystemBars();
    }

    private void hideSystemBars() {
        Window window = getWindow();
        window.setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private final class EditorView extends View {
        private static final int STICK = 0;
        private static final int A = 1;
        private static final int B = 2;
        private static final int X = 3;
        private static final int Y = 4;
        private static final int LB = 5;
        private static final int RB = 6;
        private static final int LT = 7;
        private static final int RT = 8;
        private static final int START = 9;
        private static final int BACK = 10;
        private static final int RSTICK = 11;
        private static final int COUNT = 12;

        private static final float SCALE_MIN = 0.55f;
        private static final float SCALE_MAX = 1.80f;
        private static final float OPACITY_MIN = 0.25f;
        private static final float OPACITY_MAX = 1.00f;

        private final String[] keys = {
            "stick", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "rstick"
        };
        private final String[] labels = {
            "L", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "☰", "◀", "R"
        };
        private final float[] x = {
            .135f, .865f, .927f, .803f, .865f, .075f, .925f, .075f, .925f, .555f, .445f, .680f
        };
        private final float[] y = {
            .760f, .885f, .760f, .760f, .635f, .090f, .090f, .185f, .185f, .235f, .235f, .820f
        };
        private final float[] scale = new float[COUNT];
        private final float[] opacity = new float[COUNT];
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();

        private int selected = A;
        private int dragging = -1;
        private float grabX;
        private float grabY;

        EditorView() {
            super(TouchLayoutEditorActivity.this);
            setFocusable(true);
            for (int i = 0; i < COUNT; i++) {
                scale[i] = 1.0f;
                opacity[i] = 0.72f;
            }
            load();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            canvas.drawColor(Color.rgb(9, 14, 23));
            drawBackdrop(canvas);
            for (int i = 0; i < COUNT; i++) drawControl(canvas, i);
            drawToolbar(canvas);
        }

        private void drawBackdrop(Canvas canvas) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.rgb(13, 28, 52));
            canvas.drawCircle(getWidth() * .18f, getHeight() * .78f, getHeight() * .55f, paint);
            paint.setColor(Color.rgb(20, 43, 78));
            canvas.drawCircle(getWidth() * .88f, getHeight() * .42f, getHeight() * .62f, paint);
            paint.setColor(Color.argb(34, 95, 157, 255));
            for (int i = 0; i < 5; i++) {
                float offset = getWidth() * (.18f + i * .17f);
                canvas.drawLine(offset, 0, offset - getHeight() * .65f, getHeight(), paint);
            }
        }

        private void drawControl(Canvas canvas, int index) {
            Bounds b = bounds(index);
            int alpha = Math.round(255 * opacity[index]);
            boolean face = index >= A && index <= Y;
            boolean stick = index == STICK || index == RSTICK;

            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(Math.min(150, alpha / 2), 0, 0, 0));
            if (stick || face) {
                canvas.drawCircle(b.cx + dp(3), b.cy + dp(5), b.hw * (face ? 1.08f : 1f), paint);
            } else {
                rect.set(b.cx - b.hw + dp(2), b.cy - b.hh + dp(4),
                    b.cx + b.hw + dp(2), b.cy + b.hh + dp(4));
                canvas.drawRoundRect(rect, b.hh * .72f, b.hh * .72f, paint);
            }

            if (stick) {
                paint.setColor(Color.argb(Math.round(alpha * .38f), 99, 156, 255));
                canvas.drawCircle(b.cx, b.cy, b.hw, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(2));
                paint.setColor(Color.argb(alpha, 169, 203, 255));
                canvas.drawCircle(b.cx, b.cy, b.hw, paint);
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(Color.argb(Math.round(alpha * .82f), 218, 229, 255));
                canvas.drawCircle(b.cx, b.cy, b.hw * .44f, paint);
            } else if (face) {
                int[] colors = { 0, 0xFF37B86B, 0xFFE55353, 0xFF3987E8, 0xFFF1B943 };
                int color = colors[index];
                paint.setColor(withAlpha(color, Math.round(alpha * .58f)));
                canvas.drawCircle(b.cx, b.cy, b.hw, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(2));
                paint.setColor(withAlpha(color, alpha));
                canvas.drawCircle(b.cx, b.cy, b.hw, paint);
            } else {
                paint.setColor(Color.argb(Math.round(alpha * .60f), 31, 71, 129));
                rect.set(b.cx - b.hw, b.cy - b.hh, b.cx + b.hw, b.cy + b.hh);
                canvas.drawRoundRect(rect, b.hh * .72f, b.hh * .72f, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(2));
                paint.setColor(Color.argb(alpha, 153, 194, 255));
                canvas.drawRoundRect(rect, b.hh * .72f, b.hh * .72f, paint);
            }

            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(alpha, 255, 255, 255));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            paint.setTextSize(Math.max(dp(12), Math.min(b.hw, b.hh) * (stick ? .55f : .95f)));
            Paint.FontMetrics fm = paint.getFontMetrics();
            canvas.drawText(labels[index], b.cx, b.cy - (fm.ascent + fm.descent) / 2f, paint);

            if (index == selected) {
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(3));
                paint.setColor(Color.rgb(255, 202, 74));
                if (stick || face) canvas.drawCircle(b.cx, b.cy, b.hw + dp(7), paint);
                else {
                    rect.set(b.cx - b.hw - dp(7), b.cy - b.hh - dp(7),
                        b.cx + b.hw + dp(7), b.cy + b.hh + dp(7));
                    canvas.drawRoundRect(rect, b.hh * .8f, b.hh * .8f, paint);
                }
            }
        }

        private void drawToolbar(Canvas canvas) {
            float h = dp(58);
            float top = dp(12);
            rect.set(getWidth() * .18f, top, getWidth() * .82f, top + h);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(238, 24, 31, 43));
            canvas.drawRoundRect(rect, h / 2f, h / 2f, paint);

            float[] centers = toolbarCenters();
            String[] actions = { "RESET", "SIZE −", "SIZE +", "ALPHA −", "ALPHA +", "SAVE" };
            for (int i = 0; i < actions.length; i++) {
                float halfW = toolbarHalfWidth(i);
                rect.set(centers[i] - halfW, top + dp(6), centers[i] + halfW, top + h - dp(6));
                paint.setColor(i == 5 ? Color.rgb(31, 91, 190) : Color.rgb(43, 52, 67));
                canvas.drawRoundRect(rect, dp(18), dp(18), paint);
                paint.setTextAlign(Paint.Align.CENTER);
                paint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
                paint.setTextSize(dp(13));
                paint.setColor(Color.WHITE);
                Paint.FontMetrics fm = paint.getFontMetrics();
                canvas.drawText(actions[i], centers[i], top + h / 2f - (fm.ascent + fm.descent) / 2f, paint);
            }

            String info = String.format(Locale.US, "%s  •  SIZE %d%%  •  ALPHA %d%%",
                labels[selected], Math.round(scale[selected] * 100), Math.round(opacity[selected] * 100));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            paint.setTextSize(dp(14));
            paint.setColor(Color.rgb(218, 228, 255));
            canvas.drawText(info, getWidth() / 2f, top + h + dp(24), paint);
            paint.setTypeface(android.graphics.Typeface.DEFAULT);
            paint.setTextSize(dp(12));
            paint.setColor(Color.rgb(176, 186, 201));
            canvas.drawText("Tap a control to select it • drag to move • resize and fade each control independently",
                getWidth() / 2f, top + h + dp(45), paint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            float px = event.getX();
            float py = event.getY();
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                int action = hitToolbar(px, py);
                if (action >= 0) {
                    handleToolbar(action);
                    return true;
                }
                dragging = hitControl(px, py);
                if (dragging >= 0) {
                    selected = dragging;
                    grabX = x[dragging] - px / getWidth();
                    grabY = y[dragging] - py / getHeight();
                    invalidate();
                }
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_MOVE && dragging >= 0) {
                Bounds b = bounds(dragging);
                float marginX = Math.min(.48f, b.hw / getWidth());
                float marginY = Math.min(.48f, b.hh / getHeight());
                x[dragging] = clamp(px / getWidth() + grabX, marginX, 1f - marginX);
                y[dragging] = clamp(py / getHeight() + grabY, marginY, 1f - marginY);
                invalidate();
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_UP ||
                    event.getActionMasked() == MotionEvent.ACTION_CANCEL) {
                if (event.getActionMasked() == MotionEvent.ACTION_UP) performClick();
                dragging = -1;
                return true;
            }
            return true;
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }

        private void handleToolbar(int action) {
            if (action == 0) {
                float[] dx = { .135f, .865f, .927f, .803f, .865f, .075f, .925f, .075f, .925f, .555f, .445f, .680f };
                float[] dy = { .760f, .885f, .760f, .760f, .635f, .090f, .090f, .185f, .185f, .235f, .235f, .820f };
                System.arraycopy(dx, 0, x, 0, COUNT);
                System.arraycopy(dy, 0, y, 0, COUNT);
                for (int i = 0; i < COUNT; i++) { scale[i] = 1f; opacity[i] = .72f; }
            } else if (action == 1) {
                scale[selected] = clamp(scale[selected] - .05f, SCALE_MIN, SCALE_MAX);
            } else if (action == 2) {
                scale[selected] = clamp(scale[selected] + .05f, SCALE_MIN, SCALE_MAX);
            } else if (action == 3) {
                opacity[selected] = clamp(opacity[selected] - .05f, OPACITY_MIN, OPACITY_MAX);
            } else if (action == 4) {
                opacity[selected] = clamp(opacity[selected] + .05f, OPACITY_MIN, OPACITY_MAX);
            } else if (action == 5) {
                try {
                    save();
                    Toast.makeText(TouchLayoutEditorActivity.this,
                        R.string.touch_editor_saved, Toast.LENGTH_SHORT).show();
                    finish();
                } catch (IOException exception) {
                    Toast.makeText(TouchLayoutEditorActivity.this,
                        getString(R.string.touch_editor_save_error, exception.getMessage()),
                        Toast.LENGTH_LONG).show();
                }
            }
            invalidate();
        }

        private int hitToolbar(float px, float py) {
            float top = dp(12);
            float bottom = top + dp(58);
            if (py < top || py > bottom) return -1;
            float[] centers = toolbarCenters();
            for (int i = 0; i < centers.length; i++) {
                if (Math.abs(px - centers[i]) <= toolbarHalfWidth(i)) return i;
            }
            return -1;
        }

        private float[] toolbarCenters() {
            float left = getWidth() * .205f;
            float step = getWidth() * .115f;
            return new float[] { left, left + step, left + step * 2, left + step * 3,
                left + step * 4, left + step * 5 };
        }

        private float toolbarHalfWidth(int index) {
            return getWidth() * (index == 0 || index == 5 ? .048f : .050f);
        }

        private int hitControl(float px, float py) {
            for (int i = COUNT - 1; i >= 0; i--) {
                Bounds b = bounds(i);
                float hw = Math.max(b.hw * 1.2f, dp(24));
                float hh = Math.max(b.hh * 1.2f, dp(24));
                if (px >= b.cx - hw && px <= b.cx + hw && py >= b.cy - hh && py <= b.cy + hh) {
                    return i;
                }
            }
            return -1;
        }

        private Bounds bounds(int index) {
            float vh = getHeight();
            float s = scale[index];
            float hw;
            float hh;
            if (index == STICK) hw = hh = .150f * vh * s;
            else if (index == RSTICK) hw = hh = .105f * vh * s;
            else if (index >= A && index <= Y) hw = hh = .058f * vh * s;
            else if (index >= LB && index <= RT) { hw = .075f * vh * s; hh = .036f * vh * s; }
            else hw = hh = .032f * vh * s;
            return new Bounds(x[index] * getWidth(), y[index] * getHeight(), hw, hh);
        }

        private void load() {
            File file = AppStorage.touchLayoutFile(TouchLayoutEditorActivity.this);
            File legacy = new File(AppStorage.activeGameRoot(TouchLayoutEditorActivity.this), "touch_layout.ini");
            if (!file.isFile() && legacy.isFile()) file = legacy;
            if (!file.isFile()) return;
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    new FileInputStream(file), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    int equals = line.indexOf('=');
                    if (equals <= 0) continue;
                    String key = line.substring(0, equals).trim();
                    String value = line.substring(equals + 1).trim();
                    if ("scale".equals(key)) {
                        float legacyScale = clamp(parse(value, 1f), SCALE_MIN, SCALE_MAX);
                        for (int i = 0; i < COUNT; i++) scale[i] = legacyScale;
                        continue;
                    }
                    for (int i = 0; i < COUNT; i++) {
                        if (!keys[i].equals(key)) continue;
                        String[] parts = value.split(",");
                        if (parts.length >= 2) {
                            x[i] = clamp(parse(parts[0], x[i]), 0f, 1f);
                            y[i] = clamp(parse(parts[1], y[i]), 0f, 1f);
                        }
                        if (parts.length >= 3) scale[i] = clamp(parse(parts[2], scale[i]), SCALE_MIN, SCALE_MAX);
                        if (parts.length >= 4) opacity[i] = clamp(parse(parts[3], opacity[i]), OPACITY_MIN, OPACITY_MAX);
                        break;
                    }
                }
            } catch (IOException ignored) {
                // A malformed or inaccessible legacy file simply falls back to defaults.
            }
        }

        private void save() throws IOException {
            File target = AppStorage.touchLayoutFile(TouchLayoutEditorActivity.this);
            try (FileOutputStream output = new FileOutputStream(target);
                 OutputStreamWriter writer = new OutputStreamWriter(output, StandardCharsets.UTF_8)) {
                writer.write("version=2\n");
                for (int i = 0; i < COUNT; i++) {
                    writer.write(String.format(Locale.US, "%s=%.6f,%.6f,%.3f,%.3f\n",
                        keys[i], x[i], y[i], scale[i], opacity[i]));
                }
                writer.flush();
                output.getFD().sync();
            }
        }

        private float parse(String value, float fallback) {
            try { return Float.parseFloat(value.trim()); }
            catch (NumberFormatException ignored) { return fallback; }
        }

        private float clamp(float value, float min, float max) {
            return Math.max(min, Math.min(max, value));
        }

        private int withAlpha(int color, int alpha) {
            return Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color));
        }

        private int dp(int value) {
            return Math.round(value * getResources().getDisplayMetrics().density);
        }

        private final class Bounds {
            final float cx;
            final float cy;
            final float hw;
            final float hh;
            Bounds(float cx, float cy, float hw, float hh) {
                this.cx = cx;
                this.cy = cy;
                this.hw = hw;
                this.hh = hh;
            }
        }
    }
}
