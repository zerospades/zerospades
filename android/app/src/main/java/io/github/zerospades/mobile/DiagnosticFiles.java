package io.github.zerospades.mobile;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

/** Bounded diagnostic reads, so a large log or tombstone cannot exhaust memory. */
final class DiagnosticFiles {
    private DiagnosticFiles() { }

    static String tail(File file, int limit) throws IOException {
        if (limit < 0) throw new IllegalArgumentException("negative limit");
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long size = input.length();
            int count = (int) Math.min(size, (long) limit);
            input.seek(size - count);
            byte[] bytes = new byte[count];
            input.readFully(bytes);
            return (size > count ? "[Earlier output omitted]\n" : "") +
                    new String(bytes, StandardCharsets.UTF_8);
        }
    }

    static String beginning(InputStream input, int limit) throws IOException {
        if (limit < 0) throw new IllegalArgumentException("negative limit");
        byte[] bytes = new byte[limit + 1];
        int count = 0;
        while (count < bytes.length) {
            int n = input.read(bytes, count, bytes.length - count);
            if (n < 0) break;
            if (n == 0) break;
            count += n;
        }
        return new String(bytes, 0, Math.min(count, limit), StandardCharsets.UTF_8) +
                (count > limit ? "\n[Further output omitted]" : "");
    }
}
