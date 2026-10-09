// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.ui;

import android.content.Context;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction;
import android.view.accessibility.AccessibilityNodeProvider;

/**
 * Maul UI's accessibility tree shown to Android (record mui-0008): the
 * provider of a host view, whose virtual views are the tree's shown
 * nodes. The native adapter (maul-ui/access_android.h) makes it and
 * answers its questions; each node comes back as packed numbers and
 * texts, filled in here. The host builds this file into its
 * application. Called on the main thread only.
 */
public class AccessProvider extends AccessibilityNodeProvider {
    // A node's packed numbers, as src/android.h writes them.
    private static final int CLASS = 0;
    private static final int FLAGS = 1;
    private static final int ACTIONS = 2;
    private static final int LEFT = 3;
    private static final int TOP = 4;
    private static final int RIGHT = 5;
    private static final int BOTTOM = 6;
    private static final int PARENT = 7;
    private static final int RANGE = 8;
    private static final int MINIMUM = 9;
    private static final int MAXIMUM = 10;
    private static final int CURRENT = 11;
    private static final int LIVE = 12;
    private static final int COUNT = 13;
    private static final int CHILDREN = 14;

    private static final int CHECKABLE = 1;
    private static final int CHECKED = 1 << 1;
    private static final int ENABLED = 1 << 2;
    private static final int FOCUSABLE = 1 << 3;
    private static final int FOCUSED = 1 << 4;
    private static final int SELECTED = 1 << 5;
    private static final int SCROLLABLE = 1 << 6;
    private static final int PASSWORD = 1 << 7;
    private static final int EDITABLE = 1 << 8;
    private static final int HEADING = 1 << 9;
    private static final int MULTILINE = 1 << 10;

    // The actions a node offers, as bits, and asked of native code by
    // their index.
    private static final int CLICK = 0;
    private static final int FOCUS = 1;
    private static final int CLEAR_FOCUS = 2;
    private static final int SCROLL_BACKWARD = 3;
    private static final int SCROLL_FORWARD = 4;
    private static final int SET_PROGRESS = 5;
    private static final int EXPAND = 6;
    private static final int COLLAPSE = 7;
    private static final AccessibilityAction[] OFFERED = {
        AccessibilityAction.ACTION_CLICK,
        AccessibilityAction.ACTION_FOCUS,
        AccessibilityAction.ACTION_CLEAR_FOCUS,
        AccessibilityAction.ACTION_SCROLL_BACKWARD,
        AccessibilityAction.ACTION_SCROLL_FORWARD,
        AccessibilityAction.ACTION_SET_PROGRESS,
        AccessibilityAction.ACTION_EXPAND,
        AccessibilityAction.ACTION_COLLAPSE,
    };

    // A node's texts, by kind.
    private static final int TEXT = 0;
    private static final int DESCRIPTION = 1;
    private static final int HINT = 2;
    private static final int ROLE = 3;
    private static final int STATE = 4;

    private static final String[] CLASSES = {
        "android.view.View",
        "android.widget.Button",
        "android.widget.CheckBox",
        "android.widget.RadioButton",
        "android.widget.RadioGroup",
        "android.widget.ToggleButton",
        "android.widget.EditText",
        "android.widget.SeekBar",
        "android.widget.ProgressBar",
        "android.widget.ImageView",
        "android.widget.TabWidget",
        "android.widget.GridView",
        "android.widget.ListView",
        "android.widget.Spinner",
        "android.widget.TextView",
        "android.app.Dialog",
        "android.widget.ScrollView",
    };

    private static final String ROLE_KEY = "AccessibilityNodeInfo.roleDescription";

    private final View host;
    // The native adapter, 0 once it is gone.
    private long handle;
    // The virtual view the screen reader's cursor is on.
    private int accessibilityFocus = View.NO_ID;

    private AccessProvider(View host, long handle) {
        this.host = host;
        this.handle = handle;
    }

    private static native int rootOf(long handle);

    private static native int[] nodeOf(long handle, int id);

    private static native String textOf(long handle, int id, int kind);

    private static native boolean act(long handle, int id, int action, float value);

    private static native int focusOf(long handle);

    private static native int nodeAt(long handle, float x, float y);

    @Override
    public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
        if (id == View.NO_ID) {
            AccessibilityNodeInfo info = new AccessibilityNodeInfo(host);
            host.onInitializeAccessibilityNodeInfo(info);
            int root = handle != 0 ? rootOf(handle) : View.NO_ID;
            if (root != View.NO_ID) {
                info.addChild(host, root);
            }
            return info;
        }
        int[] node = handle != 0 ? nodeOf(handle, id) : null;
        if (node == null) {
            return null;
        }
        AccessibilityNodeInfo info = new AccessibilityNodeInfo(host, id);
        info.setPackageName(host.getContext().getPackageName());
        info.setClassName(CLASSES[node[CLASS]]);
        if (node[PARENT] == View.NO_ID) {
            info.setParent(host);
        } else {
            info.setParent(host, node[PARENT]);
        }
        for (int i = 0; i < node[COUNT]; i++) {
            info.addChild(host, node[CHILDREN + i]);
        }
        place(info, node);
        state(info, node[FLAGS]);
        texts(info, id);
        if (node[RANGE] >= 0) {
            info.setRangeInfo(new AccessibilityNodeInfo.RangeInfo(node[RANGE],
                    Float.intBitsToFloat(node[MINIMUM]), Float.intBitsToFloat(node[MAXIMUM]),
                    Float.intBitsToFloat(node[CURRENT])));
        }
        info.setLiveRegion(node[LIVE]);
        for (int i = 0; i < OFFERED.length; i++) {
            if ((node[ACTIONS] & (1 << i)) != 0) {
                info.addAction(OFFERED[i]);
            }
        }
        boolean cursor = id == accessibilityFocus;
        info.setAccessibilityFocused(cursor);
        info.addAction(cursor ? AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS
                              : AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
        return info;
    }

    // The node's box on the screen, from its box in the view.
    private void place(AccessibilityNodeInfo info, int[] node) {
        Rect box = new Rect(node[LEFT], node[TOP], node[RIGHT], node[BOTTOM]);
        int[] at = new int[2];
        host.getLocationOnScreen(at);
        box.offset(at[0], at[1]);
        info.setBoundsInScreen(box);
        info.setVisibleToUser(true);
    }

    // setChecked(boolean) is the one from Android 11 on; newer platforms
    // deprecate it for a form Android 11 lacks.
    @SuppressWarnings("deprecation")
    private static void state(AccessibilityNodeInfo info, int flags) {
        info.setCheckable((flags & CHECKABLE) != 0);
        info.setChecked((flags & CHECKED) != 0);
        info.setEnabled((flags & ENABLED) != 0);
        info.setFocusable((flags & FOCUSABLE) != 0);
        info.setFocused((flags & FOCUSED) != 0);
        info.setSelected((flags & SELECTED) != 0);
        info.setScrollable((flags & SCROLLABLE) != 0);
        info.setPassword((flags & PASSWORD) != 0);
        info.setEditable((flags & EDITABLE) != 0);
        info.setHeading((flags & HEADING) != 0);
        info.setMultiLine((flags & MULTILINE) != 0);
    }

    private void texts(AccessibilityNodeInfo info, int id) {
        info.setText(textOf(handle, id, TEXT));
        info.setContentDescription(textOf(handle, id, DESCRIPTION));
        info.setHintText(textOf(handle, id, HINT));
        info.setStateDescription(textOf(handle, id, STATE));
        String role = textOf(handle, id, ROLE);
        if (role != null) {
            info.getExtras().putCharSequence(ROLE_KEY, role);
        }
    }

    @Override
    public boolean performAction(int id, int action, Bundle arguments) {
        if (id == View.NO_ID) {
            return host.performAccessibilityAction(action, arguments);
        }
        if (handle == 0 || nodeOf(handle, id) == null) {
            return false;
        }
        switch (action) {
            case AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS:
                return moveCursor(id);
            case AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS:
                return id == accessibilityFocus && moveCursor(View.NO_ID);
            default:
                break;
        }
        float value = 0.0f;
        if (action == android.R.id.accessibilityActionSetProgress && arguments != null) {
            value = arguments.getFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE);
        }
        for (int i = 0; i < OFFERED.length; i++) {
            if (OFFERED[i].getId() == action) {
                return act(handle, id, i, value);
            }
        }
        return false;
    }

    // The screen reader's cursor to a virtual view or off, told as the
    // views' own moves are.
    private boolean moveCursor(int id) {
        int old = accessibilityFocus;
        if (old == id) {
            return false;
        }
        accessibilityFocus = id;
        if (old != View.NO_ID) {
            send(old, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED, 0);
        }
        if (id != View.NO_ID) {
            send(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED, 0);
        }
        host.invalidate();
        return true;
    }

    @Override
    public AccessibilityNodeInfo findFocus(int focus) {
        int id = View.NO_ID;
        if (focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY) {
            id = accessibilityFocus;
        } else if (focus == AccessibilityNodeInfo.FOCUS_INPUT && handle != 0) {
            id = focusOf(handle);
        }
        return id != View.NO_ID ? createAccessibilityNodeInfo(id) : null;
    }

    /**
     * The virtual view under a place in the view's pixels, or
     * View.NO_ID, for touch exploration (Maul Window's Explorer).
     */
    public int virtualViewAt(float x, float y) {
        return handle != 0 ? nodeAt(handle, x, y) : View.NO_ID;
    }

    /**
     * Tells clients of a change to a virtual view, or to the host for
     * View.NO_ID: an event of a type, with content change types for a
     * content change.
     */
    void send(int id, int type, int changes) {
        ViewParent parent = host.getParent();
        AccessibilityManager manager =
                (AccessibilityManager) host.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
        // Android throws for an event sent while accessibility is off.
        if (parent == null || manager == null || !manager.isEnabled()) {
            return;
        }
        AccessibilityEvent event = new AccessibilityEvent(type);
        event.setPackageName(host.getContext().getPackageName());
        event.setSource(host, id);
        if (type == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) {
            event.setContentChangeTypes(changes);
        }
        parent.requestSendAccessibilityEvent(host, event);
    }
}
