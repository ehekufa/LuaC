package com.luac.studio;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.math.BigInteger;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;
import java.util.Calendar;
import java.util.Date;
import javax.security.auth.x500.X500Principal;

/** The signing key stays in Android Keystore and persists across builds. */
final class ApkSigningKey {
    private static final String ALIAS = "luac-project-apk-signing-v1";
    final PrivateKey key;
    final X509Certificate certificate;
    private ApkSigningKey(PrivateKey key, X509Certificate certificate) { this.key = key; this.certificate = certificate; }

    static synchronized ApkSigningKey load() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(ALIAS)) {
            Calendar end = Calendar.getInstance(); end.add(Calendar.YEAR, 30);
            KeyPairGenerator generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore");
            generator.initialize(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN | KeyProperties.PURPOSE_VERIFY)
                .setKeySize(2048).setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA512)
                .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
                .setCertificateSubject(new X500Principal("CN=LuaC Studio Project"))
                .setCertificateSerialNumber(BigInteger.ONE).setCertificateNotBefore(new Date(0))
                .setCertificateNotAfter(end.getTime()).build());
            generator.generateKeyPair();
        }
        return new ApkSigningKey((PrivateKey) store.getKey(ALIAS, null), (X509Certificate) store.getCertificate(ALIAS));
    }
}
