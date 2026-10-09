package rawframe.client;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.view.WindowManager;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The Android client's activity (D553): Maul Window's, once the files the
 * package carries under {@code assets/game} (the client's configuration and
 * the game it names) are in the app's files directory, where the client
 * reads them. They are copied when the package installed is not the one
 * last copied from, before the client's library starts; a copy that fails
 * leaves the client to say what it cannot read.
 */
public class Activity extends maul.window.Activity {
    private static final String CARRIED = "game";
    private static final String STAMP = ".unpacked";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        unpack();
        super.onCreate(savedInstanceState);
        // NativeActivity sets the window's soft input mode itself, over the
        // manifest's, to one that lets the system raise the on-screen
        // keyboard for the focused input view at start (D560): hidden until
        // the program asks for text input, which Maul Window shows it for,
        // the window still resized around it.
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }

    private void unpack() {
        File files = getFilesDir();
        File stamp = new File(files, STAMP);
        try {
            long installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
            if (stamp.exists()) {
                try (DataInputStream in = new DataInputStream(new FileInputStream(stamp))) {
                    if (in.readLong() == installed) {
                        return;
                    }
                }
            }
            copy(getAssets(), CARRIED, files);
            try (DataOutputStream out = new DataOutputStream(new FileOutputStream(stamp))) {
                out.writeLong(installed);
            }
        } catch (Exception failed) {
            stamp.delete();
        }
    }

    /** Copies the asset at {@code path}, a file or a directory, to {@code into}. */
    private static void copy(AssetManager assets, String path, File into) throws IOException {
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            into.mkdirs();
            for (String child : children) {
                copy(assets, path + "/" + child, new File(into, child));
            }
            return;
        }
        // The carried directory itself is the files directory.
        if (path.equals(CARRIED)) {
            return;
        }
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(into)) {
            byte[] chunk = new byte[65536];
            for (int read; (read = in.read(chunk)) > 0;) {
                out.write(chunk, 0, read);
            }
        }
    }
}
