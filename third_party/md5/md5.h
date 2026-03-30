/*
 * Simple MD5 implementation — public domain.
 * Based on RFC 1321 reference implementation.
 */

#ifndef MD5_H
#define MD5_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[64];
} md5_ctx;

void md5_init(md5_ctx *ctx);
void md5_update(md5_ctx *ctx, const void *data, size_t len);
void md5_final(md5_ctx *ctx, uint8_t digest[16]);
void md5_hex(const uint8_t digest[16], char hex[33]);

#endif /* MD5_H */
