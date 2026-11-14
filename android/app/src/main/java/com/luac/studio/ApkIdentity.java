package com.luac.studio;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.SecureRandom;

/** Stable identity travels with a project; it is not a signing key. */
final class ApkIdentity {
    private ApkIdentity() { }
    static synchronized String forProject(File project) throws IOException {
        File metadata = new File(project, ".luac");
        if (Files.isSymbolicLink(metadata.toPath())) throw new IOException("Invalid .luac metadata folder");
        if (!metadata.isDirectory() && !metadata.mkdir()) throw new IOException("Cannot create .luac metadata folder");
        File id = new File(metadata, "android-id");
        if (Files.isSymbolicLink(id.toPath())) throw new IOException("Invalid Android project identity");
        if (!id.exists()) {
            byte[] bytes = new byte[6]; new SecureRandom().nextBytes(bytes);
            StringBuilder value = new StringBuilder();
            for (byte b : bytes) value.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            File temporary = File.createTempFile("android-id-", ".tmp", metadata);
            try {
                Files.write(temporary.toPath(), value.toString().getBytes(StandardCharsets.US_ASCII));
                Files.move(temporary.toPath(), id.toPath());
            } finally { temporary.delete(); }
        }
        if (!id.isFile() || id.length() != 12) throw new IOException("Invalid Android project identity");
        String value = new String(Files.readAllBytes(id.toPath()), StandardCharsets.US_ASCII);
        if (!value.matches("[0-9a-f]{12}")) throw new IOException("Invalid Android project identity");
        return "com.luac.p" + value;
    }
}
