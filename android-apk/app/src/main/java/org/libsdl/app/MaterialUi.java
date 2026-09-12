package org.libsdl.app;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.Spinner;
import android.widget.TextView;

/** Small dependency-free Material 3 styling layer shared by the Android launcher screens. */
final class MaterialUi {
    final int background;
    final int surface;
    final int surfaceVariant;
    final int primary;
    final int primaryContainer;
    final int onPrimary;
    final int onPrimaryContainer;
    final int text;
    final int muted;
    final int outline;
    final int success;
    final int warning;
    final int error;

    private final Activity activity;
    private final boolean dark;

    MaterialUi(Activity activity) {
        this.activity = activity;
        dark = (activity.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
            == Configuration.UI_MODE_NIGHT_YES;
        if (dark) {
            background = Color.rgb(15, 18, 23);
            surface = Color.rgb(25, 28, 34);
            surfaceVariant = Color.rgb(38, 43, 52);
            primary = Color.rgb(166, 200, 255);
            primaryContainer = Color.rgb(19, 64, 132);
            onPrimary = Color.rgb(0, 45, 98);
            onPrimaryContainer = Color.rgb(216, 227, 255);
            text = Color.rgb(231, 232, 238);
            muted = Color.rgb(189, 195, 207);
            outline = Color.rgb(73, 80, 93);
            success = Color.rgb(121, 219, 145);
            warning = Color.rgb(255, 190, 92);
            error = Color.rgb(255, 180, 171);
        } else {
            background = Color.rgb(246, 247, 252);
            surface = Color.rgb(255, 255, 255);
            surfaceVariant = Color.rgb(232, 237, 247);
            primary = Color.rgb(20, 83, 180);
            primaryContainer = Color.rgb(218, 228, 255);
            onPrimary = Color.WHITE;
            onPrimaryContainer = Color.rgb(0, 39, 92);
            text = Color.rgb(25, 28, 34);
            muted = Color.rgb(82, 89, 101);
            outline = Color.rgb(208, 214, 225);
            success = Color.rgb(25, 120, 55);
            warning = Color.rgb(174, 100, 0);
            error = Color.rgb(186, 26, 26);
        }
    }

    void applyWindow() {
        Window window = activity.getWindow();
        window.setStatusBarColor(background);
        window.setNavigationBarColor(background);
        window.setStatusBarContrastEnforced(false);
        window.setNavigationBarContrastEnforced(false);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                int lightBars = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                controller.setSystemBarsAppearance(dark ? 0 : lightBars, lightBars);
            }
        } else {
            int visibility = window.getDecorView().getSystemUiVisibility();
            int lightBars = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            window.getDecorView().setSystemUiVisibility(dark ? visibility & ~lightBars : visibility | lightBars);
        }
    }

    void styleRoot(View view) {
        view.setBackgroundColor(background);
    }

    void styleCard(View view) {
        GradientDrawable shape = rounded(surface, 28);
        shape.setStroke(dp(1), outline);
        view.setBackground(shape);
        view.setElevation(dp(1));
    }

    void stylePrimaryButton(Button button, int iconId) {
        styleButton(button, primary, onPrimary, iconId, true);
    }

    void styleTonalButton(Button button, int iconId) {
        styleButton(button, primaryContainer, onPrimaryContainer, iconId, false);
    }

    void styleOutlinedButton(Button button, int iconId) {
        button.setAllCaps(false);
        button.setTextColor(statefulText(primary));
        button.setTextSize(14);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setGravity(android.view.Gravity.CENTER);
        button.setMinHeight(dp(52));
        button.setMinimumHeight(dp(52));
        button.setPadding(dp(16), dp(10), dp(16), dp(10));
        GradientDrawable shape = rounded(surface, 18);
        shape.setStroke(dp(1), primary);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(withAlpha(primary, 28)), shape, null));
        setIcon(button, iconId, primary);
    }

    void styleIconButton(Button button, int iconId) {
        button.setAllCaps(false);
        button.setTextColor(onPrimaryContainer);
        button.setTextSize(20);
        button.setMinHeight(dp(48));
        button.setMinimumHeight(dp(48));
        button.setPadding(dp(12), dp(8), dp(12), dp(8));
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(withAlpha(primary, 30)),
            rounded(primaryContainer, 16), null));
        setIcon(button, iconId, onPrimaryContainer);
        if (!button.isEnabled()) button.setAlpha(0.38f);
    }

    void styleSpinner(Spinner spinner) {
        spinner.setMinimumHeight(dp(52));
        spinner.setPadding(dp(14), 0, dp(10), 0);
        spinner.setPopupBackgroundDrawable(rounded(surface, 18));
        spinner.setBackground(new RippleDrawable(ColorStateList.valueOf(withAlpha(primary, 24)),
            rounded(surfaceVariant, 16), null));
    }

    void styleCheckBox(CheckBox box) {
        box.setTextColor(text);
        box.setTextSize(14);
        box.setMinHeight(dp(48));
        int[][] states = new int[][] {
            new int[] { android.R.attr.state_checked },
            new int[] {}
        };
        box.setButtonTintList(new ColorStateList(states, new int[] { primary, muted }));
    }

    void title(TextView view) {
        view.setTextColor(text);
        view.setLetterSpacing(-0.02f);
    }

    void body(TextView view) {
        view.setTextColor(muted);
        view.setLineSpacing(0, 1.08f);
    }

    GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private void styleButton(Button button, int fill, int foreground, int iconId, boolean prominent) {
        button.setAllCaps(false);
        button.setTextColor(statefulText(foreground));
        button.setTextSize(prominent ? 16 : 14);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setGravity(android.view.Gravity.CENTER);
        button.setMinHeight(dp(prominent ? 58 : 52));
        button.setMinimumHeight(dp(prominent ? 58 : 52));
        button.setPadding(dp(18), dp(10), dp(18), dp(10));
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(withAlpha(foreground, 34)),
            rounded(fill, prominent ? 22 : 18), null));
        setIcon(button, iconId, foreground);
        if (prominent) button.setElevation(dp(2));
    }

    private void setIcon(Button button, int iconId, int color) {
        if (iconId == 0) return;
        Drawable icon = activity.getDrawable(iconId);
        if (icon == null) return;
        icon = icon.mutate();
        icon.setTint(color);
        int size = dp(20);
        icon.setBounds(0, 0, size, size);
        button.setCompoundDrawablesRelative(icon, null, null, null);
        button.setCompoundDrawablePadding(dp(9));
    }

    private ColorStateList statefulText(int enabledColor) {
        return new ColorStateList(new int[][] {
            new int[] { -android.R.attr.state_enabled }, new int[] {}
        }, new int[] { withAlpha(enabledColor, 96), enabledColor });
    }

    private static int withAlpha(int color, int alpha) {
        return Color.argb(alpha, Color.red(color), Color.green(color), Color.blue(color));
    }

    int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
