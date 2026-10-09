// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.os.LocaleList;
import android.os.PowerManager;
import android.provider.Settings;

/**
 * The system's preferences and facts the library reads, which Android
 * keeps in Java only: the text scale, reduced motion, the accent, the
 * power and the preferred locales. Called on the main thread only.
 */
final class Facts {
    // Where the facts go in facts()'s answer.
    static final int TEXT_SCALE = 0;
    static final int REDUCED_MOTION = 1;
    static final int HAS_ACCENT = 2;
    static final int ACCENT = 3;
    static final int LOW_POWER = 4;
    static final int ON_BATTERY = 5;
    static final int COUNT = 6;

    private Facts() {
    }

    /**
     * The facts: the text scale in thousandths; reduced motion, which
     * Android's "remove animations" makes its animator scale 0; whether
     * there is an accent (Android 12's system palette) and it as ARGB;
     * battery saver; and the power, 1 on the battery, 0 plugged, -1
     * unknown.
     */
    static int[] facts(Activity activity) {
        int[] facts = new int[COUNT];
        facts[TEXT_SCALE] =
                Math.round(activity.getResources().getConfiguration().fontScale * 1000.0f);
        float animation = Settings.Global.getFloat(activity.getContentResolver(),
                Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f);
        facts[REDUCED_MOTION] = animation == 0.0f ? 1 : 0;
        if (Build.VERSION.SDK_INT >= 31) {
            facts[HAS_ACCENT] = 1;
            facts[ACCENT] = activity.getColor(android.R.color.system_accent1_500);
        }
        PowerManager power = activity.getSystemService(PowerManager.class);
        facts[LOW_POWER] = power != null && power.isPowerSaveMode() ? 1 : 0;
        // The battery's sticky broadcast, read without a receiver.
        Intent battery =
                activity.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int plugged = battery != null ? battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1) : -1;
        boolean present =
                battery != null && battery.getBooleanExtra(BatteryManager.EXTRA_PRESENT, false);
        facts[ON_BATTERY] = !present || plugged < 0 ? -1 : plugged == 0 ? 1 : 0;
        return facts;
    }

    /** The preferred locales, most preferred first, as BCP 47 tags with commas. */
    static String locales() {
        return LocaleList.getDefault().toLanguageTags();
    }
}
