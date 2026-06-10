/*
 * Minimal SHAKE interface required by the vendored ML-DSA-87 reference code.
 * Implemented with the portable XKCP Keccak-p[1600] backend inside OP-TEE.
 */

#ifndef HESIA_TA_MLDSA87_FIPS202_H
#define HESIA_TA_MLDSA87_FIPS202_H

#include <stddef.h>
#include <stdint.h>

#define SHAKE128_RATE 168u
#define SHAKE256_RATE 136u

typedef struct {
    uint64_t state[25];
    size_t pos;
} shake128incctx;

typedef struct {
    uint64_t state[25];
    size_t pos;
} shake256incctx;

void shake128(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen);
void shake256(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen);

void shake128_inc_init(shake128incctx *state);
void shake128_inc_absorb(shake128incctx *state,
                         const uint8_t *input, size_t inlen);
void shake128_inc_finalize(shake128incctx *state);
void shake128_inc_squeeze(uint8_t *output, size_t outlen,
                          shake128incctx *state);
void shake128_inc_ctx_release(shake128incctx *state);

void shake256_inc_init(shake256incctx *state);
void shake256_inc_absorb(shake256incctx *state,
                         const uint8_t *input, size_t inlen);
void shake256_inc_finalize(shake256incctx *state);
void shake256_inc_squeeze(uint8_t *output, size_t outlen,
                          shake256incctx *state);
void shake256_inc_ctx_release(shake256incctx *state);

#endif /* HESIA_TA_MLDSA87_FIPS202_H */
