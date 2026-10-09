// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ContentResolver;
import android.view.DragAndDropPermissions;
import android.view.DragEvent;
import android.view.View;
import java.util.ArrayList;

/**
 * Drags over the activity's view, for the library's drops: their places
 * and contents go to the library, and a drop's documents as open
 * descriptors with their names, which the library copies into the
 * application's cache, and its first text. Called on the main thread.
 */
final class Drops implements View.OnDragListener {
    // What a drag carries (mwinDragContents), and how it moves.
    private static final int FILES = 1;
    private static final int TEXT = 2;
    private static final int ENTERED = 0;
    private static final int MOVED = 1;
    private static final int LEFT = 2;

    private final Activity activity;
    private int contents;

    Drops(Activity activity) {
        this.activity = activity;
    }

    @Override
    public boolean onDrag(View view, DragEvent event) {
        long program = maul.window.Activity.program;
        switch (event.getAction()) {
            case DragEvent.ACTION_DRAG_STARTED:
                contents = contentsOf(event.getClipDescription());
                return contents != 0 && nativeTakes(program);
            case DragEvent.ACTION_DRAG_ENTERED:
                nativeDrag(program, ENTERED, event.getX(), event.getY(), contents);
                return true;
            case DragEvent.ACTION_DRAG_LOCATION:
                nativeDrag(program, MOVED, event.getX(), event.getY(), contents);
                return true;
            case DragEvent.ACTION_DRAG_EXITED:
                nativeDrag(program, LEFT, event.getX(), event.getY(), contents);
                return true;
            case DragEvent.ACTION_DROP:
                drop(program, event);
                return true;
            default:
                return true;
        }
    }

    // Text for a text type, files for any other: a document is an address
    // of any type.
    private static int contentsOf(ClipDescription description) {
        int found = 0;
        for (int i = 0; description != null && i < description.getMimeTypeCount(); i++) {
            found |= description.getMimeType(i).startsWith("text/") ? TEXT : FILES;
        }
        return found;
    }

    private void drop(long program, DragEvent event) {
        // Another application's documents are read under the drag's
        // permissions, held while they are opened.
        DragAndDropPermissions permissions = activity.requestDragAndDropPermissions(event);
        ContentResolver resolver = activity.getContentResolver();
        ClipData clip = event.getClipData();
        ArrayList<Integer> descriptors = new ArrayList<>();
        ArrayList<String> names = new ArrayList<>();
        String text = null;
        for (int i = 0; clip != null && i < clip.getItemCount(); i++) {
            ClipData.Item item = clip.getItemAt(i);
            if (item.getUri() != null) {
                descriptors.add(Documents.descriptorOf(resolver, item.getUri()));
                names.add(Documents.nameOf(resolver, item.getUri()));
            } else if (text == null) {
                CharSequence words = item.coerceToText(activity);
                text = words != null ? words.toString() : null;
            }
        }
        if (permissions != null) {
            permissions.release();
        }
        int[] opened = new int[descriptors.size()];
        for (int i = 0; i < opened.length; i++) {
            opened[i] = descriptors.get(i);
        }
        nativeDrop(program, event.getX(), event.getY(), opened, names.toArray(new String[0]),
                text);
    }

    // The library's functions, registered by it at its start: whether the
    // window takes a drag now; a drag entering, moving or leaving, at a
    // place in pixels; a drop at a place, with its documents' descriptors
    // (-1 for one that could not be opened) and names, and its text or
    // null.
    static native boolean nativeTakes(long program);

    static native void nativeDrag(long program, int how, float x, float y, int contents);

    static native void nativeDrop(long program, float x, float y, int[] descriptors,
            String[] names, String text);
}
