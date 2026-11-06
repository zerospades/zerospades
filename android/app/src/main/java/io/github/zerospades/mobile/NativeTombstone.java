package io.github.zerospades.mobile;

import java.io.ByteArrayInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/** Minimal bounded reader for Android's protobuf native tombstones (API 31+).
 * Field numbers follow AOSP debuggerd/proto/tombstone.proto. We only decode
 * the signal and the crashing thread's backtrace, not memory or log dumps. */
final class NativeTombstone {
    private static final int MAX_TOTAL = 32 * 1024 * 1024;
    private static final int MAX_THREAD = 1024 * 1024;
    private NativeTombstone() { }

    static String describe(InputStream input) throws IOException {
        Reader root = new Reader(input, MAX_TOTAL);
        Map<Long, String> threads = new HashMap<>();
        long tid = -1;
        String signal = "unknown";
        String cause = "";
        int tag;
        while ((tag = root.tag()) != -1) {
            int field = tag >>> 3, wire = tag & 7;
            if (field == 6 && wire == 0) tid = root.varint();
            else if (field == 10 && wire == 2) signal = signal(root.message(4096));
            else if (field == 15 && wire == 2) cause = cause(root.message(4096));
            else if (field == 16 && wire == 2) {
                int length = root.length();
                if (length > MAX_THREAD || threads.size() >= 128) root.skipBytes(length);
                else {
                    Reader entry = new Reader(new ByteArrayInputStream(root.bytes(length)), length);
                    long key = -1;
                    String text = null;
                    int entryTag;
                    while ((entryTag = entry.tag()) != -1) {
                        if (entryTag == 8) key = entry.varint();
                        else if (entryTag == 18) text = thread(entry.message(MAX_THREAD));
                        else entry.skip(entryTag & 7);
                    }
                    if (key >= 0 && text != null) {
                        threads.put(key, text);
                        // Later fields can contain enormous memory dumps. The
                        // signal and faulting TID precede threads in AOSP dumps.
                        if (key == tid) break;
                    }
                }
            } else root.skip(wire);
        }
        String result = "Native signal: " + signal + (cause.isEmpty() ? "" : "\nCause: " + cause);
        String crashing = threads.get(tid);
        return result + "\nCrashing thread (tid " + tid + "):\n" +
                (crashing == null ? "Backtrace unavailable.\n" : crashing);
    }

    private static String signal(Reader r) throws IOException {
        long number = 0, code = 0, address = 0;
        String name = "", codeName = "";
        boolean hasAddress = false;
        int tag;
        while ((tag = r.tag()) != -1) {
            switch (tag) {
                case 8: number = r.varint(); break;
                case 18: name = r.string(); break;
                case 24: code = r.varint(); break;
                case 34: codeName = r.string(); break;
                case 64: hasAddress = r.varint() != 0; break;
                case 72: address = r.varint(); break;
                default: r.skip(tag & 7);
            }
        }
        return name + " (" + number + "), " + codeName + " (" + code + ")" +
                (hasAddress ? ", address 0x" + Long.toHexString(address) : "");
    }

    private static String cause(Reader r) throws IOException {
        String value = "";
        int tag;
        while ((tag = r.tag()) != -1) {
            if (tag == 10) value = r.string(); else r.skip(tag & 7);
        }
        return value;
    }

    private static String thread(Reader r) throws IOException {
        String name = "unknown";
        StringBuilder frames = new StringBuilder();
        int count = 0, tag;
        while ((tag = r.tag()) != -1) {
            if (tag == 18) name = r.string();
            else if (tag == 34) {
                int length = r.length();
                if (length > 16384 || count >= 64) r.skipBytes(length);
                else {
                    frames.append("  #").append(count++).append(' ')
                            .append(frame(new Reader(new ByteArrayInputStream(r.bytes(length)), length)))
                            .append('\n');
                }
            } else r.skip(tag & 7);
        }
        return name + "\n" + (count == 0 ? "No frames recorded.\n" : frames.toString());
    }

    private static String frame(Reader r) throws IOException {
        long pc = 0, offset = 0;
        String function = "", file = "", build = "";
        int tag;
        while ((tag = r.tag()) != -1) {
            switch (tag) {
                case 8: pc = r.varint(); break;
                case 34: function = r.string(); break;
                case 40: offset = r.varint(); break;
                case 50: file = r.string(); break;
                case 66: build = r.string(); break;
                default: r.skip(tag & 7);
            }
        }
        return "pc 0x" + Long.toHexString(pc) + " " + file +
                (function.isEmpty() ? "" : " (" + function + "+" + offset + ")") +
                (build.isEmpty() ? "" : " [build " + build + "]");
    }

    private static final class Reader {
        final InputStream input;
        int remaining;
        Reader(InputStream input, int limit) { this.input = input; this.remaining = limit; }
        int one() throws IOException {
            if (remaining == 0) throw new IOException("tombstone size limit reached");
            int b = input.read();
            if (b >= 0) --remaining;
            return b;
        }
        int tag() throws IOException {
            if (remaining == 0) return -1;
            int first = one();
            if (first < 0) return -1;
            long value = varint(first);
            if (value <= 0 || value > Integer.MAX_VALUE) throw new IOException("invalid protobuf tag");
            return (int) value;
        }
        long varint() throws IOException {
            int first = one();
            if (first < 0) throw new EOFException();
            return varint(first);
        }
        long varint(int first) throws IOException {
            long result = first & 0x7f;
            int b = first;
            for (int shift = 7; (b & 0x80) != 0; shift += 7) {
                if (shift >= 70) throw new IOException("invalid protobuf varint");
                b = one();
                if (b < 0) throw new EOFException();
                result |= (long) (b & 0x7f) << shift;
            }
            return result;
        }
        int length() throws IOException {
            long value = varint();
            if (value < 0 || value > remaining) throw new IOException("invalid protobuf length");
            return (int) value;
        }
        byte[] bytes(int length) throws IOException {
            if (length < 0 || length > remaining) throw new IOException("invalid protobuf length");
            byte[] data = new byte[length];
            int pos = 0;
            while (pos < length) {
                int n = input.read(data, pos, length - pos);
                if (n <= 0) throw new EOFException();
                pos += n; remaining -= n;
            }
            return data;
        }
        void skipBytes(int length) throws IOException {
            if (length < 0 || length > remaining) throw new IOException("invalid protobuf length");
            while (length > 0) {
                long n = input.skip(length);
                if (n <= 0) { if (one() < 0) throw new EOFException(); --length; }
                else { length -= (int) n; remaining -= (int) n; }
            }
        }
        Reader message(int max) throws IOException {
            int length = length();
            if (length > max) throw new IOException("protobuf message too large");
            return new Reader(new ByteArrayInputStream(bytes(length)), length);
        }
        String string() throws IOException {
            int length = length();
            if (length > 4096) throw new IOException("protobuf string too large");
            return new String(bytes(length), StandardCharsets.UTF_8);
        }
        void skip(int wire) throws IOException {
            switch (wire) {
                case 0: varint(); break;
                case 1: skipBytes(8); break;
                case 2: skipBytes(length()); break;
                case 5: skipBytes(4); break;
                default: throw new IOException("unsupported protobuf wire type");
            }
        }
    }
}
