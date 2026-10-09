// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

/**
 * What a program's accessibility root implements, beside its
 * AccessibilityNodeProvider, to be explored by touch: the virtual view
 * under a finger. Android asks the view hosting a provider, not the
 * provider, what is under a point (ExploreByTouchHelper's
 * getVirtualViewAt); the library's view asks the root through this.
 * Called on the main thread.
 */
public interface Explorer {
    /**
     * The virtual view under a place in the window's pixels, or
     * android.view.View.NO_ID for none.
     */
    int virtualViewAt(float x, float y);
}
