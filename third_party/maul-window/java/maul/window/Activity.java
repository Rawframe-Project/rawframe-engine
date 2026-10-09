// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.view.KeyEvent;
import android.view.PointerIcon;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/**
 * The activity of a Maul Window program on Android (mwin-0026). The
 * platform's NativeActivity hands the library its window's surface and
 * input queue; this class adds what the library's C cannot do itself:
 * a view that input methods type into, the window drawn behind the
 * system's bars with their insets told, the on-screen keyboard, and the
 * mouse pointer's icon over the view.
 * Name it, or a subclass of it, in the manifest, with the program's
 * native library as {@code android.app.lib_name}.
 */
public class Activity extends NativeActivity {
    /**
     * The running program, which outlives activities: an activity created
     * while it runs joins it. Written and read by the library only; zero
     * while no program runs.
     */
    static long program;

    // The view, which Accessibility hosts the program's root on.
    View field;
    private boolean keyboard;
    private int purpose;
    // The pointer icon asked for, which a view made later takes too.
    private PointerIcon pointer;
    // Counts the input method's starts: a connection from an earlier one
    // drops its composition rather than commit it.
    private int generation;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // No title or action bar, whatever the theme.
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        super.onCreate(savedInstanceState);
        // The window covers the screen; the bars and cutouts are insets.
        Window window = getWindow();
        window.setDecorFitsSystemWindows(false);
        window.setStatusBarColor(Color.TRANSPARENT);
        window.setNavigationBarColor(Color.TRANSPARENT);
        field = new Field(this);
        field.setPointerIcon(pointer);
        setContentView(field);
        field.setOnDragListener(new Drops(this));
        field.requestFocus();
        field.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets safe = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            Insets ime = insets.getInsets(WindowInsets.Type.ime());
            nativeInsets(program, safe.left, safe.top, safe.right, safe.bottom, ime.bottom);
            return insets;
        });
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // A keyboard asked for before the window had the focus shows now.
        if (hasFocus && keyboard) {
            showKeyboard(true, purpose);
        }
    }

    @Override
    protected void onActivityResult(int request, int code, Intent data) {
        // The document picker's result goes to the library.
        if (!Documents.result(this, request, code, data)) {
            super.onActivityResult(request, code, data);
        }
    }

    /** Shows or hides the on-screen keyboard for a purpose; called by the library. */
    void showKeyboard(boolean shown, int purpose) {
        boolean changed = purpose != this.purpose;
        this.keyboard = shown;
        this.purpose = purpose;
        InputMethodManager manager = getSystemService(InputMethodManager.class);
        if (changed) {
            restartInput();
        }
        if (shown) {
            field.requestFocus();
            manager.showSoftInput(field, 0);
        } else {
            manager.hideSoftInputFromWindow(field.getWindowToken(), 0);
        }
    }

    /**
     * Starts the input method again, its composition dropped: the old
     * connection's closing finishes it, which then commits nothing.
     * Called by the library.
     */
    void restartInput() {
        generation += 1;
        getSystemService(InputMethodManager.class).restartInput(field);
    }

    /** Shows a system pointer icon over the view; called by the library. */
    void showPointerShape(int type) {
        showPointer(PointerIcon.getSystemIcon(this, type));
    }

    /**
     * Shows a pointer icon over the view, now or once the view is made;
     * called by the library.
     */
    void showPointer(PointerIcon icon) {
        pointer = icon;
        if (field != null) {
            field.setPointerIcon(icon);
        }
    }

    /**
     * A pointer icon from ARGB pixels with straight alpha, rows from the
     * top, and a hotspot in them; called by the library.
     */
    PointerIcon makePointer(int[] argb, int width, int height, float x, float y) {
        return PointerIcon.create(
                Bitmap.createBitmap(argb, width, height, Bitmap.Config.ARGB_8888), x, y);
    }

    // The library's functions, registered by it at its start. Each takes
    // the running program, and does nothing for none.
    static native void nativeCommit(long program, String text);

    static native void nativeCompose(long program, String text, int start, int end);

    static native void nativeKey(long program, int action, int code, int meta, int device);

    static native void nativeInsets(long program, int left, int top, int right, int bottom,
            int keyboard);

    // The input type of a purpose (mwinInputPurpose): nothing is corrected,
    // capitalized or completed.
    private int typeOf(int purpose) {
        int text = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        switch (purpose) {
            case 1:
                return InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL
                        | InputType.TYPE_NUMBER_FLAG_SIGNED;
            case 2:
                return text | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
            case 3:
                return InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
            case 4:
                return text | InputType.TYPE_TEXT_VARIATION_URI;
            default:
                return text;
        }
    }

    /** The view input methods type into, which draws nothing. */
    private final class Field extends View {
        Field(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            info.inputType = typeOf(purpose);
            info.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return new Connection(this);
        }

        // The program's accessibility root (Accessibility.java).
        @Override
        public AccessibilityNodeProvider getAccessibilityNodeProvider() {
            Object root = Accessibility.nativeRoot(program);
            return root instanceof AccessibilityNodeProvider
                    ? (AccessibilityNodeProvider) root
                    : super.getAccessibilityNodeProvider();
        }
    }

    /**
     * What input methods type through. The program keeps its own text, so
     * the editor holds only the composition: committed text goes to the
     * program and leaves the editor empty, and a deletion outside a
     * composition is the Backspace or Delete key.
     */
    private final class Connection extends BaseInputConnection {
        private final Editable editable = Editable.Factory.getInstance().newEditable("");
        private final int made = generation;

        Connection(View view) {
            super(view, true);
        }

        @Override
        public Editable getEditable() {
            return editable;
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            super.commitText(text, newCursorPosition);
            commitAll();
            return true;
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            super.setComposingText(text, newCursorPosition);
            compose();
            return true;
        }

        @Override
        public boolean setComposingRegion(int start, int end) {
            super.setComposingRegion(start, end);
            compose();
            return true;
        }

        @Override
        public boolean finishComposingText() {
            super.finishComposingText();
            commitAll();
            return true;
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            if (editable.length() > 0) {
                super.deleteSurroundingText(beforeLength, afterLength);
                compose();
                return true;
            }
            tap(KeyEvent.KEYCODE_DEL, beforeLength);
            tap(KeyEvent.KEYCODE_FORWARD_DEL, afterLength);
            return true;
        }

        @Override
        public boolean sendKeyEvent(KeyEvent event) {
            nativeKey(program, event.getAction(), event.getKeyCode(), event.getMetaState(),
                    event.getDeviceId());
            return true;
        }

        @Override
        public boolean performEditorAction(int actionCode) {
            tap(KeyEvent.KEYCODE_ENTER, 1);
            return true;
        }

        private void tap(int code, int count) {
            for (int i = 0; i < count; i++) {
                nativeKey(program, KeyEvent.ACTION_DOWN, code, 0, -1);
                nativeKey(program, KeyEvent.ACTION_UP, code, 0, -1);
            }
        }

        // The composition's text and its selection, relative to it; none
        // ends it. A connection the input method's start left tells nothing.
        private void compose() {
            if (made != generation) {
                return;
            }
            int start = getComposingSpanStart(editable);
            int end = getComposingSpanEnd(editable);
            if (start < 0 || end <= start) {
                nativeCompose(program, "", 0, 0);
                return;
            }
            int from = Math.max(0, Math.min(end, Selection.getSelectionStart(editable)) - start);
            int to = Math.max(0, Math.min(end, Selection.getSelectionEnd(editable)) - start);
            nativeCompose(program, editable.subSequence(start, end).toString(), from, to);
        }

        // What the editor holds was committed: it goes to the program, and
        // the composition, if any, ends; a connection the input method's
        // start left drops it.
        private void commitAll() {
            String text = editable.toString();
            editable.clear();
            removeComposingSpans(editable);
            if (made != generation) {
                return;
            }
            if (!text.isEmpty()) {
                nativeCommit(program, text);
            }
            nativeCompose(program, "", 0, 0);
        }
    }
}
