/*
 * Simple MD5 implementation — public domain.
 * Based on RFC 1321 reference implementation.
 */

#include "md5.h"
#include <string.h>

#define F(x,y,z) (((x) & (y)) | (~(x) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & ~(z)))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | ~(z)))

#define ROT(x,n) (((x) << (n)) | ((x) >> (32-(n))))

#define STEP(f,a,b,c,d,x,t,s) \
    (a) += f((b),(c),(d)) + (x) + (t); \
    (a) = ROT((a),(s)); \
    (a) += (b)

static const uint8_t padding[64] = { 0x80 };

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];

    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
                ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);

    /* Round 1 */
    STEP(F, a, b, c, d, x[ 0], 0xd76aa478,  7);
    STEP(F, d, a, b, c, x[ 1], 0xe8c7b756, 12);
    STEP(F, c, d, a, b, x[ 2], 0x242070db, 17);
    STEP(F, b, c, d, a, x[ 3], 0xc1bdceee, 22);
    STEP(F, a, b, c, d, x[ 4], 0xf57c0faf,  7);
    STEP(F, d, a, b, c, x[ 5], 0x4787c62a, 12);
    STEP(F, c, d, a, b, x[ 6], 0xa8304613, 17);
    STEP(F, b, c, d, a, x[ 7], 0xfd469501, 22);
    STEP(F, a, b, c, d, x[ 8], 0x698098d8,  7);
    STEP(F, d, a, b, c, x[ 9], 0x8b44f7af, 12);
    STEP(F, c, d, a, b, x[10], 0xffff5bb1, 17);
    STEP(F, b, c, d, a, x[11], 0x895cd7be, 22);
    STEP(F, a, b, c, d, x[12], 0x6b901122,  7);
    STEP(F, d, a, b, c, x[13], 0xfd987193, 12);
    STEP(F, c, d, a, b, x[14], 0xa679438e, 17);
    STEP(F, b, c, d, a, x[15], 0x49b40821, 22);

    /* Round 2 */
    STEP(G, a, b, c, d, x[ 1], 0xf61e2562,  5);
    STEP(G, d, a, b, c, x[ 6], 0xc040b340,  9);
    STEP(G, c, d, a, b, x[11], 0x265e5a51, 14);
    STEP(G, b, c, d, a, x[ 0], 0xe9b6c7aa, 20);
    STEP(G, a, b, c, d, x[ 5], 0xd62f105d,  5);
    STEP(G, d, a, b, c, x[10], 0x02441453,  9);
    STEP(G, c, d, a, b, x[15], 0xd8a1e681, 14);
    STEP(G, b, c, d, a, x[ 4], 0xe7d3fbc8, 20);
    STEP(G, a, b, c, d, x[ 9], 0x21e1cde6,  5);
    STEP(G, d, a, b, c, x[14], 0xc33707d6,  9);
    STEP(G, c, d, a, b, x[ 3], 0xf4d50d87, 14);
    STEP(G, b, c, d, a, x[ 8], 0x455a14ed, 20);
    STEP(G, a, b, c, d, x[13], 0xa9e3e905,  5);
    STEP(G, d, a, b, c, x[ 2], 0xfcefa3f8,  9);
    STEP(G, c, d, a, b, x[ 7], 0x676f02d9, 14);
    STEP(G, b, c, d, a, x[12], 0x8d2a4c8a, 20);

    /* Round 3 */
    STEP(H, a, b, c, d, x[ 5], 0xfffa3942,  4);
    STEP(H, d, a, b, c, x[ 8], 0x8771f681, 11);
    STEP(H, c, d, a, b, x[11], 0x6d9d6122, 16);
    STEP(H, b, c, d, a, x[14], 0xfde5380c, 23);
    STEP(H, a, b, c, d, x[ 1], 0xa4beea44,  4);
    STEP(H, d, a, b, c, x[ 4], 0x4bdecfa9, 11);
    STEP(H, c, d, a, b, x[ 7], 0xf6bb4b60, 16);
    STEP(H, b, c, d, a, x[10], 0xbebfbc70, 23);
    STEP(H, a, b, c, d, x[13], 0x289b7ec6,  4);
    STEP(H, d, a, b, c, x[ 0], 0xeaa127fa, 11);
    STEP(H, c, d, a, b, x[ 3], 0xd4ef3085, 16);
    STEP(H, b, c, d, a, x[ 6], 0x04881d05, 23);
    STEP(H, a, b, c, d, x[ 9], 0xd9d4d039,  4);
    STEP(H, d, a, b, c, x[12], 0xe6db99e5, 11);
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8, 16);
    STEP(H, b, c, d, a, x[ 2], 0xc4ac5665, 23);

    /* Round 4 */
    STEP(I, a, b, c, d, x[ 0], 0xf4292244,  6);
    STEP(I, d, a, b, c, x[ 7], 0x432aff97, 10);
    STEP(I, c, d, a, b, x[14], 0xab9423a7, 15);
    STEP(I, b, c, d, a, x[ 5], 0xfc93a039, 21);
    STEP(I, a, b, c, d, x[12], 0x655b59c3,  6);
    STEP(I, d, a, b, c, x[ 3], 0x8f0ccc92, 10);
    STEP(I, c, d, a, b, x[10], 0xffeff47d, 15);
    STEP(I, b, c, d, a, x[ 1], 0x85845dd1, 21);
    STEP(I, a, b, c, d, x[ 8], 0x6fa87e4f,  6);
    STEP(I, d, a, b, c, x[15], 0xfe2ce6e0, 10);
    STEP(I, c, d, a, b, x[ 6], 0xa3014314, 15);
    STEP(I, b, c, d, a, x[13], 0x4e0811a1, 21);
    STEP(I, a, b, c, d, x[ 4], 0xf7537e82,  6);
    STEP(I, d, a, b, c, x[11], 0xbd3af235, 10);
    STEP(I, c, d, a, b, x[ 2], 0x2ad7d2bb, 15);
    STEP(I, b, c, d, a, x[ 9], 0xeb86d391, 21);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void md5_init(md5_ctx *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t offset = (size_t)(ctx->count & 63);
    ctx->count += len;

    if (offset) {
        size_t avail = 64 - offset;
        if (len < avail) {
            memcpy(ctx->buffer + offset, p, len);
            return;
        }
        memcpy(ctx->buffer + offset, p, avail);
        md5_transform(ctx->state, ctx->buffer);
        p += avail;
        len -= avail;
    }

    while (len >= 64) {
        md5_transform(ctx->state, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(ctx->buffer, p, len);
}

void md5_final(md5_ctx *ctx, uint8_t digest[16])
{
    uint64_t bits = ctx->count * 8;
    size_t offset = (size_t)(ctx->count & 63);
    size_t padlen = (offset < 56) ? (56 - offset) : (120 - offset);

    md5_update(ctx, padding, padlen);

    uint8_t bitbuf[8];
    for (int i = 0; i < 8; i++)
        bitbuf[i] = (uint8_t)(bits >> (i * 8));
    md5_update(ctx, bitbuf, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i]);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

void md5_hex(const uint8_t digest[16], char hex[33])
{
    static const char hexchars[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex[i*2]   = hexchars[digest[i] >> 4];
        hex[i*2+1] = hexchars[digest[i] & 0x0f];
    }
    hex[32] = '\0';
}
