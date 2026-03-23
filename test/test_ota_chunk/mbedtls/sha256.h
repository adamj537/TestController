/* mbedtls/sha256.h — native test stub with real SHA-256 implementation.
 * Based on the RFC 6234 / FIPS 180-4 specification.
 * Used by test_ota_chunk to exercise the full SHA-256 validation path. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
} mbedtls_sha256_context;

static inline void mbedtls_sha256_init(mbedtls_sha256_context *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

static inline void mbedtls_sha256_free(mbedtls_sha256_context *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

/* SHA-256 constants */
static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROR32(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define S0(x) (ROR32(x,2)  ^ ROR32(x,13) ^ ROR32(x,22))
#define S1(x) (ROR32(x,6)  ^ ROR32(x,11) ^ ROR32(x,25))
#define G0(x) (ROR32(x,7)  ^ ROR32(x,18) ^ ((x) >> 3))
#define G1(x) (ROR32(x,17) ^ ROR32(x,19) ^ ((x) >> 10))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static inline void sha256_transform(mbedtls_sha256_context *ctx, const uint8_t *data)
{
    uint32_t a,b,c,d,e,f,g,h,T1,T2,W[64];
    int i;
    for (i = 0; i < 16; i++)
        W[i] = ((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)|
               ((uint32_t)data[i*4+2]<<8)|(uint32_t)data[i*4+3];
    for (i = 16; i < 64; i++)
        W[i] = G1(W[i-2]) + W[i-7] + G0(W[i-15]) + W[i-16];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i = 0; i < 64; i++) {
        T1 = h + S1(e) + CH(e,f,g) + SHA256_K[i] + W[i];
        T2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+T1; d=c; c=b; b=a; a=T1+T2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static inline int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{
    (void)is224;
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    return 0;
}

static inline int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                                         const unsigned char *input, size_t ilen)
{
    size_t fill;
    uint32_t left = (uint32_t)(ctx->count & 0x3F);
    if (ilen == 0) return 0;
    ctx->count += ilen;
    fill = 64 - left;
    if (left && ilen >= fill) {
        memcpy(ctx->buffer + left, input, fill);
        sha256_transform(ctx, ctx->buffer);
        input += fill; ilen -= fill; left = 0;
    }
    while (ilen >= 64) {
        sha256_transform(ctx, input);
        input += 64; ilen -= 64;
    }
    if (ilen > 0) memcpy(ctx->buffer + left, input, ilen);
    return 0;
}

static inline int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char *output)
{
    uint64_t bits = ctx->count * 8;
    uint32_t last = (uint32_t)(ctx->count & 0x3F);
    uint32_t padn = (last < 56) ? (56 - last) : (120 - last);
    uint8_t  msglen[8];
    uint8_t  pad[64];
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    msglen[0]=(uint8_t)(bits>>56); msglen[1]=(uint8_t)(bits>>48);
    msglen[2]=(uint8_t)(bits>>40); msglen[3]=(uint8_t)(bits>>32);
    msglen[4]=(uint8_t)(bits>>24); msglen[5]=(uint8_t)(bits>>16);
    msglen[6]=(uint8_t)(bits>>8);  msglen[7]=(uint8_t)(bits);
    mbedtls_sha256_update(ctx, pad, padn);
    mbedtls_sha256_update(ctx, msglen, 8);
    int i;
    for (i = 0; i < 8; i++) {
        output[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        output[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        output[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        output[i*4+3] = (uint8_t)(ctx->state[i]);
    }
    return 0;
}
