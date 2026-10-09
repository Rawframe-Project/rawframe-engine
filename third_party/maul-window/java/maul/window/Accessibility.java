// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.view.View;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

/**
 * The program's accessibility root on the library's view: the view's
 * provider is the root, and touch exploration, whose hovers reach the
 * library's native input rather than the view, finds the virtual view
 * under the finger through the root's Explorer and announces it, as
 * ExploreByTouchHelper does. Called on the main thread only.
 */
final class Accessibility {
    // How a hover moves, as MotionEvent's actions.
    private static final int HOVER_MOVE = 7;
    private static final int HOVER_EXIT = 10;

    private Accessibility() {
    }

    /** The view the library hosts the root on. */
    static View viewOf(android.app.Activity activity) {
        return ((Activity) activity).field;
    }

    /**
     * Clients read the view's tree again: the view's root announces a
     * content change of its subtree, as the views' own changes do.
     */
    static void changed(View view) {
        ViewParent parent = view.getParent();
        if (parent != null) {
            parent.notifySubtreeAccessibilityStateChanged(
                    view, view, AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
        }
    }

    /**
     * A hover of touch exploration at a place in pixels: the virtual view
     * under it entered and the one before left. Returns the virtual view
     * hovered now, or View.NO_ID.
     */
    static int explore(View view, Object root, int action, float x, float y, int hovered) {
        int now = View.NO_ID;
        if (action != HOVER_EXIT && root instanceof Explorer) {
            now = ((Explorer) root).virtualViewAt(x, y);
        }
        if (now != hovered) {
            AccessibilityNodeProvider provider = (AccessibilityNodeProvider) root;
            if (now != View.NO_ID) {
                announce(view, provider, now, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
            }
            if (hovered != View.NO_ID) {
                announce(view, provider, hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
            }
        }
        return now;
    }

    // An event for a virtual view, filled from its node.
    private static void announce(View view, AccessibilityNodeProvider provider, int id, int type) {
        AccessibilityNodeInfo node = provider.createAccessibilityNodeInfo(id);
        AccessibilityEvent event = AccessibilityEvent.obtain(type);
        if (node != null) {
            if (node.getText() != null) {
                event.getText().add(node.getText());
            }
            event.setContentDescription(node.getContentDescription());
            event.setClassName(node.getClassName());
            event.setEnabled(node.isEnabled());
            event.setChecked(node.isChecked());
            event.setPassword(node.isPassword());
            event.setScrollable(node.isScrollable());
        }
        event.setPackageName(view.getContext().getPackageName());
        event.setSource(view, id);
        send(view, event);
    }

    private static void send(View view, AccessibilityEvent event) {
        ViewParent parent = view.getParent();
        if (parent != null) {
            parent.requestSendAccessibilityEvent(view, event);
        }
    }

    // The library's function, registered by it at its start: the root of
    // the program's window, or null, the first call telling the program a
    // client asked.
    static native Object nativeRoot(long program);
}
