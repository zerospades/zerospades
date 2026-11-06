package io.github.zerospades.mobile;

import android.content.Context;
import android.util.Base64;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.util.Enumeration;

/** Snapshot Android's enabled system trust anchors for the native OpenSSL/curl client. */
final class SystemCaBundle {
    static final String FILE_NAME = "system-ca-bundle.pem";
    private SystemCaBundle() { }

    static void install(Context context) throws IOException {
        File dir = context.getFilesDir();
        File tmp = File.createTempFile("ca-bundle-", ".tmp", dir);
        try {
            int count = 0;
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                KeyStore store = KeyStore.getInstance("AndroidCAStore");
                store.load(null);
                Enumeration<String> aliases = store.aliases();
                while (aliases.hasMoreElements()) {
                    String alias = aliases.nextElement();
                    // Match the default trust policy for modern Android apps:
                    // user-installed anchors are not automatically trusted.
                    if (!alias.startsWith("system:")) continue;
                    Certificate cert = store.getCertificate(alias);
                    if (cert == null) continue;
                    String encoded = Base64.encodeToString(cert.getEncoded(), Base64.NO_WRAP);
                    out.write("-----BEGIN CERTIFICATE-----\n".getBytes(StandardCharsets.US_ASCII));
                    for (int i = 0; i < encoded.length(); i += 64) {
                        out.write(encoded.substring(i, Math.min(i + 64, encoded.length()))
                                .getBytes(StandardCharsets.US_ASCII));
                        out.write('\n');
                    }
                    out.write("-----END CERTIFICATE-----\n".getBytes(StandardCharsets.US_ASCII));
                    ++count;
                }
                if (count == 0) throw new IOException("No Android system trust anchors available");
                out.getFD().sync();
            } catch (IOException error) {
                throw error;
            } catch (Exception error) {
                throw new IOException("Cannot export Android system trust anchors", error);
            }
            if (!tmp.renameTo(new File(dir, FILE_NAME)))
                throw new IOException("Cannot install Android trust bundle");
        } finally {
            if (tmp.exists() && !tmp.delete()) tmp.deleteOnExit();
        }
    }
}
