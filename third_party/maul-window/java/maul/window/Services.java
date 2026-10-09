// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.net.Uri;
import java.nio.charset.StandardCharsets;

/**
 * What the library asks of Android's services, which its C reaches only
 * through Java: the clipboard and opening addresses. Called by the
 * library on the main thread only.
 */
final class Services {
    private Services() {
    }

    /** Puts text, UTF-8, on the clipboard: false when Android refused. */
    static boolean writeClipboard(Activity activity, byte[] text) {
        ClipboardManager clipboard = activity.getSystemService(ClipboardManager.class);
        if (clipboard == null) {
            return false;
        }
        String string = new String(text, StandardCharsets.UTF_8);
        clipboard.setPrimaryClip(ClipData.newPlainText(null, string));
        return true;
    }

    /**
     * The clipboard's text: its first item as text, empty for none. A
     * read Android refuses (a window without the focus, from Android 10)
     * reads as empty, Android telling it apart from none only in its log.
     */
    static String readClipboard(Activity activity) {
        ClipboardManager clipboard = activity.getSystemService(ClipboardManager.class);
        if (clipboard == null) {
            return null;
        }
        ClipData clip = clipboard.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) {
            return "";
        }
        CharSequence text = clip.getItemAt(0).coerceToText(activity);
        return text != null ? text.toString() : "";
    }

    /**
     * Opens an address, UTF-8, in the program the user chose for it:
     * false when there is none.
     */
    static boolean openUrl(Activity activity, byte[] address) {
        Uri uri = Uri.parse(new String(address, StandardCharsets.UTF_8));
        try {
            activity.startActivity(new Intent(Intent.ACTION_VIEW, uri));
            return true;
        } catch (ActivityNotFoundException | SecurityException e) {
            return false;
        }
    }
}
