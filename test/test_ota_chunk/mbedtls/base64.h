/* mbedtls/base64.h — native test stub with real base64 implementation.
 * Used by test_ota_chunk to exercise the base64 extraction + decode path
 * in ota_chunk_dcmd without pulling in the full ESP-IDF mbedTLS library. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL  -0x002A
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER -0x002C

/* Minimal base64 decode — standard alphabet, no line breaks. */
static inline int mbedtls_base64_decode(unsigned char *dst, size_t dlen,
                                        size_t *olen,
                                        const unsigned char *src, size_t slen)
{
    /* Decode table: 255 = invalid */
    static const unsigned char T[256] = {
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
         52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
        255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
         15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
        255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
         41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    };

    size_t i, out = 0;
    /* Estimate output size */
    size_t n = slen;
    if (n == 0) { if (olen) *olen = 0; return 0; }

    /* Count padding */
    size_t pad = 0;
    if (slen >= 1 && src[slen-1] == '=') pad++;
    if (slen >= 2 && src[slen-2] == '=') pad++;

    size_t decoded_size = (slen / 4) * 3 - pad;
    if (olen) *olen = decoded_size;
    if (!dst) return 0;
    if (dlen < decoded_size) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;

    uint32_t acc = 0;
    int      bits = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = src[i];
        if (c == '=') break;
        unsigned char v = T[c];
        if (v == 255) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            dst[out++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }

    return 0;
}
