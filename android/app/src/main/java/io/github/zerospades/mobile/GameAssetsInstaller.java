package io.github.zerospades.mobile;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;

/**
 * Installs bundled paks into the native engine's private filesystem. Call from
 * a worker before starting SDL, never while the native client is using the files.
 * This is not used by the launcher-only preview.
 */
public final class GameAssetsInstaller {
    private static final String ASSET_DIR = "game-paks/";
    private static final String[] PAKS = {
        "pak000-Nonfree.pak", "pak002-Base.pak", "pak005-Models.pak",
        "pak010-BaseSkin.pak", "pak050-Locales.pak", "pak999-References.pak",
        "font-unifont.pak"
    };
    private GameAssetsInstaller() { }

    public static synchronized void install(Context context) throws IOException {
        AssetManager assets = context.getAssets();
        // Check presence before changing the installed files.
        for (String name : PAKS) {
            try (InputStream ignored = assets.open(ASSET_DIR + name)) { }
        }
        File dir = new File(context.getFilesDir(), "game-paks");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create " + dir);
        for (String name : PAKS) {
            File destination = new File(dir, name);
            File tmp = File.createTempFile("pak-", ".tmp", dir);
            try {
                MessageDigest incoming = digest();
                try (InputStream in = assets.open(ASSET_DIR + name);
                     FileOutputStream out = new FileOutputStream(tmp)) {
                    byte[] buffer = new byte[32768];
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        incoming.update(buffer, 0, n);
                        out.write(buffer, 0, n);
                    }
                    out.getFD().sync();
                }
                if (destination.isFile() && Arrays.equals(incoming.digest(), hash(destination))) continue;
                // Both files are in the same directory, so rename is atomic on Android.
                if (!tmp.renameTo(destination)) throw new IOException("Cannot replace " + destination);
            } finally {
                if (tmp.exists() && !tmp.delete()) tmp.deleteOnExit();
            }
        }
    }
    private static byte[] hash(File path) throws IOException {
        MessageDigest digest = digest();
        try (InputStream in = new FileInputStream(path)) {
            byte[] buffer = new byte[32768];
            int n;
            while ((n = in.read(buffer)) != -1) digest.update(buffer, 0, n);
        }
        return digest.digest();
    }
    private static MessageDigest digest() throws IOException {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IOException("SHA-256 unavailable", e); }
    }
}
