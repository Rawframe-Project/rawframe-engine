// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.ui;

import android.content.Context;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.View;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction;
import android.view.accessibility.AccessibilityNodeProvider;
import java.util.Collections;

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
    private static final int GRANULARITIES = 13;
    private static final int SELECTION_START = 14;
    private static final int SELECTION_END = 15;
    private static final int COUNT = 16;
    private static final int CHILDREN = 17;

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
    private static final int SET_SELECTION = 8;
    private static final int SET_TEXT = 9;
    private static final AccessibilityAction[] OFFERED = {
        AccessibilityAction.ACTION_CLICK,
        AccessibilityAction.ACTION_FOCUS,
        AccessibilityAction.ACTION_CLEAR_FOCUS,
        AccessibilityAction.ACTION_SCROLL_BACKWARD,
        AccessibilityAction.ACTION_SCROLL_FORWARD,
        AccessibilityAction.ACTION_SET_PROGRESS,
        AccessibilityAction.ACTION_EXPAND,
        AccessibilityAction.ACTION_COLLAPSE,
        AccessibilityAction.ACTION_SET_SELECTION,
        AccessibilityAction.ACTION_SET_TEXT,
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
    private static final String GRANULARITY_ARGUMENT =
            AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT;
    private static final String EXTEND_ARGUMENT =
            AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN;
    private static final String LOCATION_KEY =
            AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY;
    private static final String LOCATION_START_ARGUMENT =
            AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX;
    private static final String LOCATION_LENGTH_ARGUMENT =
            AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH;
    private static final String SELECTION_START_ARGUMENT =
            AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT;
    private static final String SELECTION_END_ARGUMENT =
            AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT;
    private static final String SET_TEXT_ARGUMENT =
            AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE;

    private final View host;
    // The native adapter, 0 once it is gone.
    private long handle;
    // The virtual view the screen reader's cursor is on, and where in its
    // text the cursor moving by granularity is (UTF-16, -1 for nowhere)
    // when it is not text being edited, whose caret it is.
    private int accessibilityFocus = View.NO_ID;
    private int traversed = -1;

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

    private static native int[] traverse(long handle, int id, int granularity, boolean forward,
            boolean extend, int cursor);

    private static native boolean select(long handle, int id, int start, int end);

    private static native boolean setText(long handle, int id, String text);

    private static native float[] characterBoxes(long handle, int id, int start, int length);

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
        if (node[GRANULARITIES] != 0) {
            info.setMovementGranularities(node[GRANULARITIES]);
            info.addAction(AccessibilityAction.ACTION_NEXT_AT_MOVEMENT_GRANULARITY);
            info.addAction(AccessibilityAction.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY);
        }
        if (node[SELECTION_START] >= 0) {
            info.setTextSelection(node[SELECTION_START], node[SELECTION_END]);
        }
        if (node[GRANULARITIES] != 0) {
            info.setAvailableExtraData(Collections.singletonList(LOCATION_KEY));
        }
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

    /**
     * Where a node's text's characters are on the screen, for
     * EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY: a RectF for each UTF-16 unit
     * of the start and length asked for, null for one with none.
     */
    @Override
    public void addExtraDataToAccessibilityNodeInfo(int id, AccessibilityNodeInfo info,
            String key, Bundle arguments) {
        if (handle == 0 || !LOCATION_KEY.equals(key) || arguments == null) {
            return;
        }
        int start = arguments.getInt(LOCATION_START_ARGUMENT, -1);
        int length = arguments.getInt(LOCATION_LENGTH_ARGUMENT, -1);
        float[] boxes = characterBoxes(handle, id, start, length);
        if (boxes == null) {
            return;
        }
        int[] at = new int[2];
        host.getLocationOnScreen(at);
        RectF[] places = new RectF[length];
        for (int i = 0; i < length; i++) {
            if (!Float.isNaN(boxes[4 * i])) {
                places[i] = new RectF(boxes[4 * i] + at[0], boxes[4 * i + 1] + at[1],
                        boxes[4 * i + 2] + at[0], boxes[4 * i + 3] + at[1]);
            }
        }
        info.getExtras().putParcelableArray(key, places);
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
            case AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY:
                return moveThrough(id, action, arguments, true);
            case AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY:
                return moveThrough(id, action, arguments, false);
            case AccessibilityNodeInfo.ACTION_SET_SELECTION:
                return selectAsked(id, arguments);
            case AccessibilityNodeInfo.ACTION_SET_TEXT: {
                CharSequence text =
                        arguments != null ? arguments.getCharSequence(SET_TEXT_ARGUMENT) : null;
                return setText(handle, id, text != null ? text.toString() : "");
            }
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

    // A selection asked for, or the caret where it is without one.
    private boolean selectAsked(int id, Bundle arguments) {
        boolean given = arguments != null && arguments.containsKey(SELECTION_START_ARGUMENT)
                && arguments.containsKey(SELECTION_END_ARGUMENT);
        return given
                ? select(handle, id, arguments.getInt(SELECTION_START_ARGUMENT),
                        arguments.getInt(SELECTION_END_ARGUMENT))
                : select(handle, id, -1, -1);
    }

    // A move through a node's text by a granularity, told as a view's
    // traversal is: the segment passed, the action and the granularity.
    private boolean moveThrough(int id, int action, Bundle arguments, boolean forward) {
        if (arguments == null) {
            return false;
        }
        int granularity = arguments.getInt(GRANULARITY_ARGUMENT);
        boolean extend = arguments.getBoolean(EXTEND_ARGUMENT);
        int cursor = id == accessibilityFocus ? traversed : -1;
        int[] moved = traverse(handle, id, granularity, forward, extend, cursor);
        if (moved == null) {
            return false;
        }
        if (id == accessibilityFocus) {
            traversed = moved[3];
        }
        AccessibilityEvent event =
                event(id, AccessibilityEvent.TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY);
        if (event != null) {
            event.getText().add(textOf(handle, id, TEXT));
            event.setFromIndex(moved[0]);
            event.setToIndex(moved[1]);
            event.setAction(action);
            event.setMovementGranularity(granularity);
            host.getParent().requestSendAccessibilityEvent(host, event);
        }
        return true;
    }

    // The screen reader's cursor to a virtual view or off, told as the
    // views' own moves are; where it is in a text is forgotten.
    private boolean moveCursor(int id) {
        int old = accessibilityFocus;
        if (old == id) {
            return false;
        }
        accessibilityFocus = id;
        traversed = -1;
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
        AccessibilityEvent event = event(id, type);
        if (event == null) {
            return;
        }
        if (type == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) {
            event.setContentChangeTypes(changes);
        }
        host.getParent().requestSendAccessibilityEvent(host, event);
    }

    /**
     * Tells clients of a text event on a virtual view, in UTF-16: a text
     * changed from an index, how much went and how much came, with the
     * text before; or a selection changed, its start, its end and the
     * text's length.
     */
    void sendText(int id, int type, int a, int b, int c, String before) {
        AccessibilityEvent event = event(id, type);
        if (event == null || handle == 0) {
            return;
        }
        event.getText().add(textOf(handle, id, TEXT));
        event.setFromIndex(a);
        if (type == AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED) {
            event.setRemovedCount(b);
            event.setAddedCount(c);
            event.setBeforeText(before != null ? before : "");
        } else {
            event.setToIndex(b);
            event.setItemCount(c);
        }
        host.getParent().requestSendAccessibilityEvent(host, event);
    }

    // A new event of a type from a virtual view; null while accessibility
    // is off, when Android throws for one sent, or with no parent.
    private AccessibilityEvent event(int id, int type) {
        ViewParent parent = host.getParent();
        AccessibilityManager manager =
                (AccessibilityManager) host.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
        if (parent == null || manager == null || !manager.isEnabled()) {
            return null;
        }
        AccessibilityEvent event = new AccessibilityEvent(type);
        event.setPackageName(host.getContext().getPackageName());
        event.setSource(host, id);
        return event;
    }
}
