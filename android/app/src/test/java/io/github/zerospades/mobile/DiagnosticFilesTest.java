package io.github.zerospades.mobile;

import org.junit.Test;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

public class DiagnosticFilesTest {
    @Test public void fileTailIsBoundedAndMarksTruncation() throws Exception {
        File file = File.createTempFile("zs-diagnostics", ".txt");
        try {
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write("0123456789".getBytes(StandardCharsets.UTF_8));
            }
            assertEquals("0123456789", DiagnosticFiles.tail(file, 10));
            assertEquals("[Earlier output omitted]\n6789", DiagnosticFiles.tail(file, 4));
            assertEquals("[Earlier output omitted]\n", DiagnosticFiles.tail(file, 0));
        } finally { file.delete(); }
    }

    @Test public void tombstoneReadIsBounded() throws Exception {
        byte[] input = "abcdefgh".getBytes(StandardCharsets.UTF_8);
        assertEquals("abcd\n[Further output omitted]",
                DiagnosticFiles.beginning(new ByteArrayInputStream(input), 4));
        assertEquals("abcdefgh", DiagnosticFiles.beginning(new ByteArrayInputStream(input), 8));
    }
}
