package com.luac.studio;

import com.android.apksig.ApkSigner;
import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.FilterOutputStream;
import java.io.OutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;
import java.util.Collections;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.CRC32;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/** Inserts a validated project into the unsigned player and signs the result. */
final class ApkBuilder {
    static final String PROJECT_ASSET = "assets/luac-project.luac";
    private ApkBuilder() { }

    private static final class CountingStream extends FilterOutputStream {
        long count;
        CountingStream(OutputStream output) { super(output); }
        @Override public void write(int value) throws IOException { out.write(value); count++; }
        @Override public void write(byte[] bytes, int offset, int length) throws IOException {
            out.write(bytes, offset, length); count += length;
        }
    }

    static void build(InputStream template, File project, File unsigned, File output,
                      PrivateKey key, X509Certificate certificate) throws Exception {
        if (!new File(project, "main.lua").isFile()) throw new IOException("main.lua is missing");
        try (ZipInputStream zip = new ZipInputStream(template);
             CountingStream counted = new CountingStream(new FileOutputStream(unsigned));
             ZipOutputStream result = new ZipOutputStream(counted)) {
            ZipEntry item;
            byte[] buffer = new byte[65536];
            while ((item = zip.getNextEntry()) != null) {
                String name = item.getName();
                String upper = name.toUpperCase(Locale.ROOT);
                if (name.equals(PROJECT_ASSET)) throw new IOException("Player template already contains a project");
                // A template can contain an old JAR signature; it must not survive repacking.
                if (upper.startsWith("META-INF/") && (upper.endsWith(".SF") || upper.endsWith(".RSA") ||
                    upper.endsWith(".DSA") || upper.endsWith(".EC") || upper.equals("META-INF/MANIFEST.MF"))) continue;
                ZipEntry copy = new ZipEntry(name);
                if (name.equals("resources.arsc")) {
                    // Android 11+ requires this entry to be stored and four-byte aligned.
                    ByteArrayOutputStream data = new ByteArrayOutputStream();
                    int count;
                    while ((count = zip.read(buffer)) != -1) {
                        if (data.size() + count > 16 * 1024 * 1024) throw new IOException("Resources are too large");
                        data.write(buffer, 0, count);
                    }
                    byte[] bytes = data.toByteArray();
                    CRC32 crc = new CRC32(); crc.update(bytes);
                    copy.setMethod(ZipEntry.STORED); copy.setSize(bytes.length); copy.setCompressedSize(bytes.length); copy.setCrc(crc.getValue());
                    long headerEnd = counted.count + 30 + name.getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
                    int padding = (int) ((4 - (headerEnd % 4)) % 4);
                    if (padding != 0) {
                        // ZIP extra fields need a four-byte header. Field 0xd935 is Android alignment padding.
                        byte[] extra = new byte[padding + 4];
                        extra[0] = 0x35; extra[1] = (byte) 0xd9; extra[2] = (byte) padding;
                        copy.setExtra(extra);
                    }
                    result.putNextEntry(copy); result.write(bytes);
                } else {
                    result.putNextEntry(copy);
                    int count;
                    while ((count = zip.read(buffer)) != -1) result.write(buffer, 0, count);
                }
                result.closeEntry();
            }
            result.putNextEntry(new ZipEntry(PROJECT_ASSET));
            ProjectArchive.write(project, result);
            result.closeEntry();
        }
        ApkSigner.SignerConfig signer = new ApkSigner.SignerConfig.Builder("LUAC", key,
            Collections.singletonList(certificate)).build();
        new ApkSigner.Builder(Collections.singletonList(signer))
            .setInputApk(unsigned).setOutputApk(output).setMinSdkVersion(26)
            .setV1SigningEnabled(true).setV2SigningEnabled(true).setV3SigningEnabled(false).build().sign();
    }
}
