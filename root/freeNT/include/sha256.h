#ifndef FREENT_SHA256_H
#define FREENT_SHA256_H

#include "types.h"

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint32_t buflen;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]);
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/* HMAC-SHA256 (RFC 2104). Used to verify .trp packages downloaded over
 * trpm's plain-HTTP transport actually came from someone holding the
 * shared secret, and weren't substituted in-flight by an on-path
 * attacker (see TRP_HMAC_SECRET in config.h). This is a *shared-secret*
 * scheme, not a public-key signature - anyone with the secret can forge
 * a valid tag, so it protects against network tampering between the
 * proxy and this OS, not against someone who already has the secret. */
void hmac_sha256(const uint8_t *key, size_t keylen,
                  const uint8_t *msg, size_t msglen,
                  uint8_t out[32]);

/* Constant-time comparison of two 32-byte digests - avoids leaking
 * how many leading bytes matched via early-exit timing. */
int sha256_digest_equal(const uint8_t a[32], const uint8_t b[32]);

#endif
