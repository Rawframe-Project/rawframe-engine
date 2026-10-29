// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.Activity;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.Display;
import java.lang.reflect.Method;

/**
 * The activity's display, the library's one monitor, as far as Android
 * tells it in Java only: its size, density, refresh rate and HDR facts
 *. Called by the library on the main thread only.
 */
final class Screen {
    // Where the facts go in facts()'s answer.
    static final int WIDTH = 0;
    static final int HEIGHT = 1;
    static final int X_DPI = 2;
    static final int Y_DPI = 3;
    static final int REFRESH = 4;
    static final int HDR = 5;
    static final int PEAK = 6;
    static final int AVERAGE = 7;
    static final int RATIO = 8;
    static final int ADAPTIVE = 9;
    static final int COUNT = 10;

    private Screen() {
    }

    /**
     * The facts, all 0 without a display: its whole size in pixels and its
     * dots per inch across and down; its refresh rate in hertz; 1 where it
     * can show HDR; the desired peak and frame-average luminance in nits,
     * 0 where unknown; the HDR/SDR ratio now (Android 14), 0 where
     * unknown; and whether it supports adaptive refresh, 2 for yes and 1
     * for no (Android 16), 0 where unknown.
     */
    static float[] facts(Activity activity) {
        float[] facts = new float[COUNT];
        Display display = activity.getDisplay();
        if (display == null) {
            return facts;
        }
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);
        facts[WIDTH] = metrics.widthPixels;
        facts[HEIGHT] = metrics.heightPixels;
        facts[X_DPI] = metrics.xdpi;
        facts[Y_DPI] = metrics.ydpi;
        facts[REFRESH] = display.getRefreshRate();
        facts[HDR] = display.isHdr() ? 1.0f : 0.0f;
        Display.HdrCapabilities hdr = display.getHdrCapabilities();
        if (hdr != null) {
            facts[PEAK] = Math.max(hdr.getDesiredMaxLuminance(), 0.0f);
            facts[AVERAGE] = Math.max(hdr.getDesiredMaxAverageLuminance(), 0.0f);
        }
        if (Build.VERSION.SDK_INT >= 34 && display.isHdrSdrRatioAvailable()) {
            facts[RATIO] = display.getHdrSdrRatio();
        }
        facts[ADAPTIVE] = adaptive(display);
        return facts;
    }

    /**
     * Whether the display supports adaptive refresh, which Android 16
     * tells: 2 for yes, 1 for no, 0 where unknown. Reached by reflection,
     * as the library builds against an older SDK.
     */
    private static float adaptive(Display display) {
        if (Build.VERSION.SDK_INT < 36) {
            return 0.0f;
        }
        try {
            Method method = Display.class.getMethod("hasArrSupport");
            Object supported = method.invoke(display);
            return supported instanceof Boolean ? ((Boolean) supported ? 2.0f : 1.0f) : 0.0f;
        } catch (ReflectiveOperationException | RuntimeException error) {
            return 0.0f;
        }
    }

    /** The display's name, or null without one. */
    static String name(Activity activity) {
        Display display = activity.getDisplay();
        return display != null ? display.getName() : null;
    }
}
