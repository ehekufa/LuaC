#ifndef LUAC_TLS_H
#define LUAC_TLS_H

/* mbedTLS transport for the native HTTP client. Enabled explicitly with
   LUAC_WITH_MBEDTLS. Plain HTTP and all non-network libraries remain usable
   without this dependency. */
#ifdef LUAC_WITH_MBEDTLS
#include <mbedtls/build_info.h>
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
#include <psa/crypto.h>
#else
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#endif
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#ifdef _WIN32
#include <wincrypt.h>
#endif

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context private_key;
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
#endif
    LcOsSocket *handle;
    int active;
    char error[320];
} LcTls;

static void lc_tls_message(LcTls *tls, int code, const char *operation) {
    char detail[160];
    mbedtls_strerror(code, detail, sizeof(detail));
    snprintf(tls->error, sizeof(tls->error), "%s: %s (%d)", operation, detail, code);
}
static int lc_tls_random_init(LcTls *tls, const char *personalization) {
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
    (void)tls; (void)personalization;
    return psa_crypto_init();
#else
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_ctr_drbg_init(&tls->random);
    return mbedtls_ctr_drbg_seed(&tls->random, mbedtls_entropy_func, &tls->entropy,
        (const unsigned char *)personalization, strlen(personalization));
#endif
}
static void lc_tls_random_cleanup(LcTls *tls) {
#if MBEDTLS_VERSION_NUMBER < 0x04000000
    mbedtls_ctr_drbg_free(&tls->random);
    mbedtls_entropy_free(&tls->entropy);
#else
    (void)tls;
#endif
}
static void lc_tls_random_configure(LcTls *tls) {
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
    /* mbedTLS 4 obtains TLS randomness from its initialized PSA subsystem. */
    (void)tls;
#else
    mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->random);
#endif
}
static void lc_tls_cleanup(LcTls *tls) {
    if (tls->active) mbedtls_ssl_close_notify(&tls->ssl);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    mbedtls_x509_crt_free(&tls->ca);
    mbedtls_x509_crt_free(&tls->certificate);
    mbedtls_pk_free(&tls->private_key);
    lc_tls_random_cleanup(tls);
    tls->active = 0;
}
static int lc_tls_send_socket(void *context, const unsigned char *data, size_t length) {
    LcOsSocket *handle = (LcOsSocket *)context;
    int sent = send(*handle, (const char *)data, (int)(length > INT32_MAX ? INT32_MAX : length), 0);
    return sent < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : sent;
}
static int lc_tls_read_socket(void *context, unsigned char *data, size_t length) {
    LcOsSocket *handle = (LcOsSocket *)context;
    int got = recv(*handle, (char *)data, (int)(length > INT32_MAX ? INT32_MAX : length), 0);
    return got < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : got;
}
static int lc_tls_load_ca(LcContext *ctx, LcTls *tls) {
    int result = MBEDTLS_ERR_X509_FILE_IO_ERROR;
#ifdef _WIN32
    if (ctx->tls_ca_file[0]) result = mbedtls_x509_crt_parse_file(&tls->ca, ctx->tls_ca_file);
    else {
        HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
        PCCERT_CONTEXT certificate = NULL;
        int loaded = 0;
        if (!store) { snprintf(tls->error, sizeof(tls->error), "cannot open Windows ROOT certificate store"); return -1; }
        while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL) {
            if (mbedtls_x509_crt_parse(&tls->ca, certificate->pbCertEncoded, certificate->cbCertEncoded) == 0) ++loaded;
        }
        CertCloseStore(store, 0);
        result = loaded ? 0 : MBEDTLS_ERR_X509_FILE_IO_ERROR;
    }
#else
    static const char *paths[] = {
        "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem", NULL
    };
    if (ctx->tls_ca_file[0]) result = mbedtls_x509_crt_parse_file(&tls->ca, ctx->tls_ca_file);
    else {
        int i;
        for (i = 0; paths[i]; ++i) if (access(paths[i], R_OK) == 0) {
            result = mbedtls_x509_crt_parse_file(&tls->ca, paths[i]);
            if (result == 0) break;
        }
    }
#endif
    if (result != 0) lc_tls_message(tls, result, "cannot load trusted CA certificates");
    return result;
}
static int lc_tls_start(LcContext *ctx, LcTls *tls, LcOsSocket *handle, const char *host) {
    int result;
    const char *personalization = "LuaC/0.5 HTTPS";
    memset(tls, 0, sizeof(*tls));
    tls->handle = handle;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_x509_crt_init(&tls->ca);
    mbedtls_x509_crt_init(&tls->certificate);
    mbedtls_pk_init(&tls->private_key);
    result = lc_tls_random_init(tls, personalization);
    if (result != 0) { lc_tls_message(tls, result, "TLS random initialization"); goto failed; }
    if (lc_tls_load_ca(ctx, tls) != 0) goto failed;
    result = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) { lc_tls_message(tls, result, "TLS configuration"); goto failed; }
    mbedtls_ssl_conf_authmode(&tls->config, ctx->tls_verify ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->ca, NULL);
    lc_tls_random_configure(tls);
    result = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (result != 0) { lc_tls_message(tls, result, "TLS setup"); goto failed; }
    result = mbedtls_ssl_set_hostname(&tls->ssl, host);
    if (result != 0) { lc_tls_message(tls, result, "TLS hostname"); goto failed; }
    mbedtls_ssl_set_bio(&tls->ssl, handle, lc_tls_send_socket, lc_tls_read_socket, NULL);
    while ((result = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            lc_tls_message(tls, result, "TLS handshake"); goto failed;
        }
    }
    if (ctx->tls_verify) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
        if (flags != 0) {
            char verification[192];
            mbedtls_x509_crt_verify_info(verification, sizeof(verification), "certificate verification failed", flags);
            snprintf(tls->error, sizeof(tls->error), "%s", verification);
            goto failed;
        }
    }
    tls->active = 1;
    return 0;
failed:
    if (!tls->error[0]) snprintf(tls->error, sizeof(tls->error), "TLS initialization failed");
    /* Avoid close_notify on a partially negotiated transport. */
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    mbedtls_x509_crt_free(&tls->ca);
    mbedtls_x509_crt_free(&tls->certificate);
    mbedtls_pk_free(&tls->private_key);
    lc_tls_random_cleanup(tls);
    tls->active = 0;
    return -1;
}
static int lc_tls_write(LcTls *tls, const char *data, size_t length) {
    int result;
    do result = mbedtls_ssl_write(&tls->ssl, (const unsigned char *)data, (int)(length > INT32_MAX ? INT32_MAX : length));
    while (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE ||
           result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (result < 0) lc_tls_message(tls, result, "TLS write");
    return result;
}
static int lc_tls_read(LcTls *tls, char *data, size_t length) {
    int result;
    do result = mbedtls_ssl_read(&tls->ssl, (unsigned char *)data, (int)(length > INT32_MAX ? INT32_MAX : length));
    while (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE ||
           result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (result < 0) { lc_tls_message(tls, result, "TLS read"); return -1; }
    return result;
}
static int lc_tls_start_server(LcContext *ctx, LcTls *tls, LcOsSocket *handle, const char *certificate, const char *private_key) {
    int result; const char *personalization = "LuaC/0.5 WSS server";
    (void)ctx; memset(tls, 0, sizeof(*tls)); tls->handle = handle;
    mbedtls_ssl_init(&tls->ssl); mbedtls_ssl_config_init(&tls->config);
    mbedtls_x509_crt_init(&tls->ca); mbedtls_x509_crt_init(&tls->certificate); mbedtls_pk_init(&tls->private_key);
    result = lc_tls_random_init(tls, personalization);
    if (result != 0) { lc_tls_message(tls, result, "TLS random initialization"); goto failed; }
    result = mbedtls_x509_crt_parse_file(&tls->certificate, certificate);
    if (result != 0) { lc_tls_message(tls, result, "TLS server certificate"); goto failed; }
#if MBEDTLS_VERSION_NUMBER >= 0x04000000
    result = mbedtls_pk_parse_keyfile(&tls->private_key, private_key, NULL);
#else
    result = mbedtls_pk_parse_keyfile(&tls->private_key, private_key, NULL, mbedtls_ctr_drbg_random, &tls->random);
#endif
    if (result != 0) { lc_tls_message(tls, result, "TLS server private key"); goto failed; }
    result = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) { lc_tls_message(tls, result, "TLS server configuration"); goto failed; }
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_NONE);
    lc_tls_random_configure(tls);
    result = mbedtls_ssl_conf_own_cert(&tls->config, &tls->certificate, &tls->private_key);
    if (result != 0) { lc_tls_message(tls, result, "TLS server identity"); goto failed; }
    result = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (result != 0) { lc_tls_message(tls, result, "TLS server setup"); goto failed; }
    mbedtls_ssl_set_bio(&tls->ssl, handle, lc_tls_send_socket, lc_tls_read_socket, NULL);
    while ((result = mbedtls_ssl_handshake(&tls->ssl)) != 0) {
        if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) { lc_tls_message(tls, result, "TLS server handshake"); goto failed; }
    }
    tls->active = 1; return 0;
failed:
    if (!tls->error[0]) snprintf(tls->error, sizeof(tls->error), "TLS server initialization failed");
    mbedtls_ssl_free(&tls->ssl); mbedtls_ssl_config_free(&tls->config);
    mbedtls_x509_crt_free(&tls->ca); mbedtls_x509_crt_free(&tls->certificate); mbedtls_pk_free(&tls->private_key);
    lc_tls_random_cleanup(tls); tls->active = 0; return -1;
}
#else
typedef struct { char error[320]; } LcTls;
static int lc_tls_start(LcContext *ctx, LcTls *tls, LcOsSocket *handle, const char *host) {
    (void)ctx; (void)handle; (void)host;
    snprintf(tls->error, sizeof(tls->error), "HTTPS is disabled: rebuild with LUAC_WITH_MBEDTLS and mbedTLS");
    return -1;
}
static int lc_tls_start_server(LcContext *ctx, LcTls *tls, LcOsSocket *handle, const char *certificate, const char *private_key) { (void)ctx; (void)handle; (void)certificate; (void)private_key; (void)tls; snprintf(tls->error, sizeof(tls->error), "WSS server is disabled: rebuild with mbedTLS"); return -1; }
static int lc_tls_write(LcTls *tls, const char *data, size_t length) { (void)data; (void)length; snprintf(tls->error, sizeof(tls->error), "TLS is unavailable"); return -1; }
static int lc_tls_read(LcTls *tls, char *data, size_t length) { (void)data; (void)length; snprintf(tls->error, sizeof(tls->error), "TLS is unavailable"); return -1; }
static void lc_tls_cleanup(LcTls *tls) { (void)tls; }
#endif

static LcValue lc_tls_failure(LcContext *ctx, LcTls *tls) {
    LcValue out[2]; out[0] = lc_nil(); out[1] = lc_string(ctx, tls->error);
    return lc_tuple_new(ctx, 2, out, 1);
}
#endif
