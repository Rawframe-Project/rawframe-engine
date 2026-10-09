// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

package maul.window;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.Locale;

/**
 * The document picker, for the library's file dialogs: Android's
 * documents are content addresses, never paths, so each chosen one is
 * handed to the library as an open descriptor with its name, which the
 * library copies into the application's cache. Called on the main thread
 * only.
 */
final class Documents {
    /**
     * The request codes of the picker's results: the high bits mark them
     * the library's, the low byte numbers the dialog.
     */
    static final int REQUEST = 0x4d00;

    private Documents() {
    }

    /**
     * Shows the picker for one document or many, of the extensions given
     * (';' between them; empty for any), under a dialog's number, taking
     * away the picker of the dialog before, if it still shows: false when
     * there is none.
     */
    static boolean open(Activity activity, boolean many, String extensions, int number,
            int before) {
        activity.finishActivity(REQUEST | before);
        ArrayList<String> types = new ArrayList<>();
        MimeTypeMap map = MimeTypeMap.getSingleton();
        for (String extension : extensions.split(";")) {
            String type = map.getMimeTypeFromExtension(extension.toLowerCase(Locale.ROOT));
            if (type != null && !types.contains(type)) {
                types.add(type);
            }
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, many);
        // Extensions Android knows no type of offer every document.
        boolean any = extensions.isEmpty() || types.isEmpty();
        intent.setType(any || types.size() > 1 ? "*/*" : types.get(0));
        if (!any && types.size() > 1) {
            intent.putExtra(Intent.EXTRA_MIME_TYPES, types.toArray(new String[0]));
        }
        try {
            activity.startActivityForResult(intent, REQUEST | number);
            return true;
        } catch (ActivityNotFoundException e) {
            return false;
        }
    }

    /**
     * The picker's result, from the activity's onActivityResult: the
     * documents chosen go to the library, opened, or none for a picker
     * closed. False for a result that is not the picker's.
     */
    static boolean result(Activity activity, int request, int code, Intent data) {
        if ((request & ~0xff) != REQUEST) {
            return false;
        }
        ArrayList<Uri> chosen = new ArrayList<>();
        if (code == Activity.RESULT_OK && data != null) {
            ClipData clip = data.getClipData();
            for (int i = 0; clip != null && i < clip.getItemCount(); i++) {
                chosen.add(clip.getItemAt(i).getUri());
            }
            if (chosen.isEmpty() && data.getData() != null) {
                chosen.add(data.getData());
            }
        }
        ContentResolver resolver = activity.getContentResolver();
        int[] descriptors = new int[chosen.size()];
        String[] names = new String[chosen.size()];
        for (int i = 0; i < descriptors.length; i++) {
            descriptors[i] = descriptorOf(resolver, chosen.get(i));
            names[i] = nameOf(resolver, chosen.get(i));
        }
        nativeDocuments(maul.window.Activity.program, request & 0xff,
                code == Activity.RESULT_OK, descriptors, names);
        return true;
    }

    // A document opened to read, its descriptor the library's; -1 when
    // it could not be.
    static int descriptorOf(ContentResolver resolver, Uri uri) {
        try {
            ParcelFileDescriptor descriptor = resolver.openFileDescriptor(uri, "r");
            return descriptor != null ? descriptor.detachFd() : -1;
        } catch (FileNotFoundException | SecurityException | IllegalArgumentException e) {
            return -1;
        }
    }

    // A document's name as its provider tells it, or empty.
    static String nameOf(ContentResolver resolver, Uri uri) {
        String[] columns = {OpenableColumns.DISPLAY_NAME};
        try (Cursor cursor = resolver.query(uri, columns, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getString(0);
            }
        } catch (SecurityException | IllegalArgumentException e) {
            // A provider that tells no name.
        }
        return "";
    }

    /**
     * The library's function, registered by it at its start: the dialog's
     * number, whether its picker was chosen from, the documents'
     * descriptors (-1 for one that could not be opened) and names.
     */
    static native void nativeDocuments(long program, int number, boolean chosen,
            int[] descriptors, String[] names);
}
