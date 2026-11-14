package com.luac.studio;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

/** Updates equal-length package names in Android's binary XML string pool. */
final class BinaryManifest {
    private static final String TEMPLATE_PACKAGE = "com.luac.projectrunner";
    private BinaryManifest() { }

    static void setPackage(byte[] bytes, String packageName) throws IOException {
        if (!packageName.matches("com\\.luac\\.p[0-9a-f]{12}") || packageName.length() != TEMPLATE_PACKAGE.length())
            throw new IOException("Invalid package identity");
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (bytes.length < 36 || u16(data, 0) != 3 || u16(data, 2) < 8 || u32(data, 4) != bytes.length)
            throw new IOException("Invalid binary Android manifest");
        int chunk = u16(data, 2);
        if (chunk + 28 > bytes.length || u16(data, chunk) != 1) throw new IOException("Manifest string pool missing");
        int header = u16(data, chunk + 2), chunkSize = u32(data, chunk + 4);
        int count = u32(data, chunk + 8), start = u32(data, chunk + 20);
        if (header < 28 || count < 0 || chunkSize < header || start < header ||
            (long) chunk + chunkSize > bytes.length || (long) header + (long) count * 4 > chunkSize || start >= chunkSize)
            throw new IOException("Invalid manifest string pool");
        boolean utf8 = (u32(data, chunk + 16) & 0x100) != 0, found = false;
        Charset charset = utf8 ? StandardCharsets.UTF_8 : StandardCharsets.UTF_16LE;
        for (int i = 0; i < count; i++) {
            int offset = u32(data, chunk + header + i * 4);
            long absolute = (long) chunk + start + offset;
            if (offset < 0 || absolute >= (long) chunk + chunkSize) throw new IOException("Invalid manifest string offset");
            int[] cursor = {(int) absolute};
            int length;
            if (utf8) { length8(bytes, cursor, chunk + chunkSize); length = length8(bytes, cursor, chunk + chunkSize); }
            else length = length16(data, cursor, chunk + chunkSize) * 2;
            if (length < 0 || (long) cursor[0] + length > chunk + chunkSize) throw new IOException("Invalid manifest string length");
            String value = new String(bytes, cursor[0], length, charset);
            if (value.equals(TEMPLATE_PACKAGE)) found = true;
            if (value.equals(TEMPLATE_PACKAGE) || value.startsWith(TEMPLATE_PACKAGE + ".")) {
                byte[] replacement = (packageName + value.substring(TEMPLATE_PACKAGE.length())).getBytes(charset);
                if (replacement.length != length) throw new IOException("Manifest package replacement size mismatch");
                System.arraycopy(replacement, 0, bytes, cursor[0], length);
            }
        }
        if (!found) throw new IOException("Template package not found in manifest");
    }
    private static int length8(byte[] b, int[] p, int end) throws IOException {
        if (p[0] >= end) throw new IOException("Truncated manifest string");
        int first = b[p[0]++] & 255;
        if ((first & 0x80) == 0) return first;
        if (p[0] >= end) throw new IOException("Truncated manifest string");
        return ((first & 0x7f) << 8) | (b[p[0]++] & 255);
    }
    private static int length16(ByteBuffer b, int[] p, int end) throws IOException {
        if (p[0] + 2 > end) throw new IOException("Truncated manifest string");
        int first = u16(b, p[0]); p[0] += 2;
        if ((first & 0x8000) == 0) return first;
        if (p[0] + 2 > end) throw new IOException("Truncated manifest string");
        int result = ((first & 0x7fff) << 16) | u16(b, p[0]); p[0] += 2;
        if (result < 0 || result > Integer.MAX_VALUE / 2) throw new IOException("Manifest string too large");
        return result;
    }
    private static int u16(ByteBuffer b, int pos) { return b.getShort(pos) & 0xffff; }
    private static int u32(ByteBuffer b, int pos) { return b.getInt(pos); }
}
