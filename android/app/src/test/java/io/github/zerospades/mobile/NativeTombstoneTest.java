package io.github.zerospades.mobile;

import org.junit.Test;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import static org.junit.Assert.*;

public class NativeTombstoneTest {
    private static byte[] varint(long value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        while ((value & ~127L) != 0) { out.write(((int) value & 127) | 128); value >>>= 7; }
        out.write((int) value); return out.toByteArray();
    }
    private static byte[] join(byte[]... parts) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] part : parts) out.write(part);
        return out.toByteArray();
    }
    private static byte[] number(int field, long value) throws IOException {
        return join(varint(field << 3), varint(value));
    }
    private static byte[] data(int field, byte[] value) throws IOException {
        return join(varint((field << 3) | 2), varint(value.length), value);
    }
    private static byte[] text(int field, String value) throws IOException {
        return data(field, value.getBytes(StandardCharsets.UTF_8));
    }
    private static byte[] thread(long tid, String name, String lib) throws IOException {
        byte[] frame = join(number(1, 0x1234), text(4, "run"), number(5, 4), text(6, lib));
        byte[] body = join(number(1, tid), text(2, name), data(4, frame));
        return data(16, join(number(1, tid), data(2, body)));
    }

    @Test public void describesCrashingThreadNotOtherThreads() throws Exception {
        byte[] signal = join(number(1, 11), text(2, "SIGSEGV"), number(3, 1),
                text(4, "SEGV_MAPERR"), number(8, 1), number(9, 0));
        byte[] tombstone = join(number(6, 42), data(10, signal),
                data(15, text(1, "null pointer dereference")),
                thread(10, "other", "libother.so"), thread(42, "SDLThread", "libzerospades.so"),
                // This trailing huge field has no contents: it must not be read
                // after the crashing thread has already been found.
                varint((17 << 3) | 2), varint(40000000));
        String result = NativeTombstone.describe(new ByteArrayInputStream(tombstone));
        assertTrue(result.contains("SIGSEGV (11), SEGV_MAPERR (1), address 0x0"));
        assertTrue(result.contains("null pointer dereference"));
        assertTrue(result.contains("SDLThread"));
        assertTrue(result.contains("pc 0x1234 libzerospades.so (run+4)"));
        assertFalse(result.contains("libother.so"));
    }

    @Test public void rejectsInvalidOrTruncatedData() throws Exception {
        try {
            NativeTombstone.describe(new ByteArrayInputStream(new byte[] { (byte) 0x82, 1, 5, 8 }));
            fail("expected malformed tombstone to be rejected");
        } catch (IOException expected) { }
    }
}
