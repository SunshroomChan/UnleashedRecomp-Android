package org.libsdl.app;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/** Paths shared by the launcher, document provider and native Android storage policy. */
final class AppStorage {
    private static final String SELECTED_ROOT_FILE = "selected_game_root.txt";

    private AppStorage() {}

    /**
     * App-specific dir under Android/media. Unlike Android/data, on-device file
     * managers can browse it on Android 11+, so it serves as a PC-less fallback
     * for game files and driver imports. Null when external storage is unavailable.
     */
    static File mediaBase(Context context) {
        File[] dirs = context.getExternalMediaDirs();
        return (dirs != null && dirs.length > 0) ? dirs[0] : null;
    }

    /** A user-selected shared-storage root, also consumed by native storage_android.cpp. */
    static File selectedGameRoot(Context context) {
        File marker = new File(context.getFilesDir(), SELECTED_ROOT_FILE);
        if (!marker.isFile()) return null;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(marker), StandardCharsets.UTF_8))) {
            String path = reader.readLine();
            if (path == null || path.trim().isEmpty()) return null;
            return new File(path.trim()).getCanonicalFile();
        } catch (IOException ignored) {
            return null;
        }
    }

    static void selectGameRoot(Context context, File directory) throws IOException {
        File canonical = directory.getCanonicalFile();
        File marker = new File(context.getFilesDir(), SELECTED_ROOT_FILE);
        try (FileOutputStream output = new FileOutputStream(marker, false)) {
            output.write((canonical.getPath() + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    static void clearSelectedGameRoot(Context context) {
        new File(context.getFilesDir(), SELECTED_ROOT_FILE).delete();
    }

    /** Mirrors native GetDataRoot(): selected shared folder, then populated legacy locations. */
    static File activeGameRoot(Context context) {
        File selected = selectedGameRoot(context);
        if (selected != null) return selected;

        File internal = new File(context.getFilesDir(), "UnleashedRecomp");
        if (new File(internal, "game").isDirectory()) {
            return internal;
        }

        File externalBase = context.getExternalFilesDir(null);
        File external = externalBase != null ? new File(externalBase, "UnleashedRecomp") : null;
        if (external != null && new File(external, "game").isDirectory()) {
            return external;
        }

        File media = mediaBase(context);
        if (media != null) {
            File mediaRoot = new File(media, "UnleashedRecomp");
            if (new File(mediaRoot, "game").isDirectory()) {
                return mediaRoot;
            }
        }

        return external != null ? external : internal;
    }

    static File configFile(Context context) {
        return new File(activeGameRoot(context), ".config/UnleashedRecomp/config.toml");
    }

    /** Stable location for the touch layout; available before any game files exist. */
    static File touchLayoutFile(Context context) {
        return new File(context.getFilesDir(), "touch_layout.ini");
    }

    /** Where native paths.cpp keeps save data: <game root>/.config/UnleashedRecomp/save. */
    static File saveDir(Context context) {
        return new File(activeGameRoot(context), ".config/UnleashedRecomp/save");
    }

    static File transferRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        return external != null ? external : context.getFilesDir();
    }

    /** Primary import folder; SAF imports and marker files go here. */
    static File driverImportDir(Context context) {
        return new File(transferRoot(context), "driver_import");
    }

    /** All folders the native side scans for dropped drivers, primary first. */
    static File[] driverImportDirs(Context context) {
        File media = mediaBase(context);
        return media != null
            ? new File[] { driverImportDir(context), new File(media, "driver_import") }
            : new File[] { driverImportDir(context) };
    }
}
