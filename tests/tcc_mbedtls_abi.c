#include <stddef.h>
#include <stdio.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#define SHOW(type) do { struct wrapper { char c; type value; }; \
    printf(#type " %zu %zu\n", sizeof(type), offsetof(struct wrapper, value)); } while (0)
int main(void) {
#if defined(MBEDTLS_THREADING_PTHREAD)
    SHOW(pthread_mutex_t);
    SHOW(mbedtls_threading_mutex_t);
    SHOW(mbedtls_entropy_context);
    SHOW(mbedtls_ctr_drbg_context);
#else
#error Expected the winpthreads mbedTLS build
#endif
    SHOW(mbedtls_ssl_context);
    SHOW(mbedtls_ssl_config);
    SHOW(mbedtls_x509_crt);
    SHOW(mbedtls_pk_context);
    return 0;
}
