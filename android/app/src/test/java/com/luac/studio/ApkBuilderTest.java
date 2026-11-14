package com.luac.studio;

import static org.junit.Assert.*;
import com.android.apksig.ApkVerifier;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.cert.X509Certificate;
import java.util.Date;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import javax.security.auth.x500.X500Principal;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ApkBuilderTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    @Test public void signedPlayerContainsProjectAndVerifies() throws Exception {
        File template = new File(System.getProperty("runner.template"));
        assertTrue("Runner template missing: " + template, template.isFile());
        File project = temp.newFolder("Game"), assets = new File(project, "assets");
        assertTrue(assets.mkdir());
        Files.write(new File(project, "main.lua").toPath(), "print('APK project')".getBytes(StandardCharsets.UTF_8));
        byte[] asset = {0, 1, (byte) 255};
        Files.write(new File(assets, "pixel.bin").toPath(), asset);
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA"); generator.initialize(2048);
        KeyPair pair = generator.generateKeyPair();
        X500Principal subject = new X500Principal("CN=Test Only");
        X509Certificate cert = new JcaX509CertificateConverter().getCertificate(
            new JcaX509v3CertificateBuilder(subject, BigInteger.ONE, new Date(0),
                new Date(System.currentTimeMillis() + 86400000), subject, pair.getPublic())
                .build(new JcaContentSignerBuilder("SHA256withRSA").build(pair.getPrivate())));
        File unsigned = new File(temp.getRoot(), "unsigned.apk"), signed = new File(temp.getRoot(), "project.apk");
        try (InputStream in = new FileInputStream(template)) { ApkBuilder.build(in, project, unsigned, signed, pair.getPrivate(), cert); }
        ApkVerifier.Result verified = new ApkVerifier.Builder(signed).setMinCheckedPlatformVersion(26).build().verify();
        assertTrue(verified.getErrors().toString(), verified.isVerified());
        assertTrue(verified.isVerifiedUsingV2Scheme());
        try (ZipFile zip = new ZipFile(signed)) {
            assertNotNull(zip.getEntry("AndroidManifest.xml"));
            assertNotNull(zip.getEntry("classes.dex"));
            assertNotNull("native runtime missing", zip.getEntry("lib/arm64-v8a/libluac_native.so"));
            ZipEntry resources = zip.getEntry("resources.arsc");
            if (resources != null) {
                assertEquals(ZipEntry.STORED, resources.getMethod());
                assertTrue("resources.arsc must be zipaligned", isAligned(signed, "resources.arsc"));
            }
            try (InputStream in = zip.getInputStream(zip.getEntry(ApkBuilder.PROJECT_ASSET))) {
                ProjectArchive.Document archive = ProjectArchive.read(in);
                assertEquals("Game", archive.name);
                boolean found = false;
                for (ProjectArchive.Entry file : archive.files) if (file.path.equals("assets/pixel.bin")) {
                    assertArrayEquals(asset, file.bytes); found = true;
                }
                assertTrue(found);
            }
        }
    }

    private boolean isAligned(File file, String name) throws Exception {
        byte[] zip = Files.readAllBytes(file.toPath()), needle = name.getBytes(StandardCharsets.UTF_8);
        // Locate the central directory record and then the corresponding local header.
        for (int i = 0; i + 46 + needle.length <= zip.length; i++) {
            if (le32(zip, i) != 0x02014b50L || le16(zip, i + 28) != needle.length) continue;
            boolean match = true;
            for (int j = 0; j < needle.length; j++) if (zip[i + 46 + j] != needle[j]) { match = false; break; }
            if (!match) continue;
            int offset = (int) le32(zip, i + 42);
            return le32(zip, offset) == 0x04034b50L &&
                (offset + 30 + le16(zip, offset + 26) + le16(zip, offset + 28)) % 4 == 0;
        }
        return false;
    }
    private static int le16(byte[] b, int p) { return (b[p] & 255) | ((b[p+1] & 255) << 8); }
    private static long le32(byte[] b, int p) { return ((long)le16(b, p) | ((long)le16(b, p + 2) << 16)); }
}
