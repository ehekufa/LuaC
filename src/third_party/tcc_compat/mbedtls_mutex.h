#ifndef LUAC_TCC_MBEDTLS_MUTEX_H
#define LUAC_TCC_MBEDTLS_MUTEX_H
/* winpthreads defines pthread_mutex_t as intptr_t. Only mbedTLS's public
   context layout needs this type; LuaC does not call pthread functions here. */
#include <stdint.h>
typedef intptr_t pthread_mutex_t;
#endif
