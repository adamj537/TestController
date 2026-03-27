/* mbedtls/base64.h — native test stub.
 * recipe_json.c includes this for the set-b64 console command which is not
 * exercised in native unit tests. */
#pragma once
#include <stddef.h>

/* Return 0 on success, MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL, or
 * MBEDTLS_ERR_BASE64_INVALID_CHARACTER. */
static inline int mbedtls_base64_decode(unsigned char *dst, size_t dlen,
                                        size_t *olen,
                                        const unsigned char *src, size_t slen)
{
    (void)dst; (void)dlen; (void)olen; (void)src; (void)slen;
    return -1; /* stub — not called in native tests */
}
