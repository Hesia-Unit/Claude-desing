#include "fips202.h"

#include <string.h>

#include "../../../keccak_plain64/KeccakP-1600-SnP.h"

typedef struct {
    uint64_t state[25];
    size_t pos;
} hesia_keccak_inc_ctx;

static void secure_zero_memory(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static void keccak_inc_reset(hesia_keccak_inc_ctx *ctx)
{
    KeccakP1600_Initialize(ctx->state);
    ctx->pos = 0;
}

static void keccak_inc_absorb(hesia_keccak_inc_ctx *ctx, size_t rate,
                              const uint8_t *input, size_t inlen)
{
    size_t chunk = 0;

    if (input == NULL && inlen != 0) {
        return;
    }

    if (ctx->pos != 0) {
        chunk = rate - ctx->pos;
        if (chunk > inlen) {
            chunk = inlen;
        }
        if (chunk != 0) {
            KeccakP1600_AddBytes(ctx->state, input,
                                 (unsigned int)ctx->pos,
                                 (unsigned int)chunk);
            ctx->pos += chunk;
            input += chunk;
            inlen -= chunk;
        }
        if (ctx->pos == rate) {
            KeccakP1600_Permute_24rounds(ctx->state);
            ctx->pos = 0;
        }
    }

    while (inlen >= rate) {
        KeccakP1600_AddBytes(ctx->state, input, 0, (unsigned int)rate);
        KeccakP1600_Permute_24rounds(ctx->state);
        input += rate;
        inlen -= rate;
    }

    if (inlen != 0) {
        KeccakP1600_AddBytes(ctx->state, input, 0, (unsigned int)inlen);
        ctx->pos = inlen;
    }
}

static void keccak_inc_finalize(hesia_keccak_inc_ctx *ctx, size_t rate,
                                uint8_t domain_sep)
{
    KeccakP1600_AddByte(ctx->state, domain_sep, (unsigned int)ctx->pos);
    KeccakP1600_AddByte(ctx->state, 0x80, (unsigned int)(rate - 1));
    ctx->pos = 0;
}

static void keccak_inc_squeeze(uint8_t *output, size_t outlen,
                               hesia_keccak_inc_ctx *ctx, size_t rate)
{
    size_t chunk = 0;
    size_t offset = 0;

    while (outlen != 0) {
        if (ctx->pos == 0) {
            KeccakP1600_Permute_24rounds(ctx->state);
            ctx->pos = rate;
        }

        chunk = ctx->pos;
        if (chunk > outlen) {
            chunk = outlen;
        }
        offset = rate - ctx->pos;
        KeccakP1600_ExtractBytes(ctx->state, output,
                                 (unsigned int)offset,
                                 (unsigned int)chunk);
        output += chunk;
        outlen -= chunk;
        ctx->pos -= chunk;
    }
}

void shake128(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen)
{
    shake128incctx ctx;

    shake128_inc_init(&ctx);
    shake128_inc_absorb(&ctx, input, inlen);
    shake128_inc_finalize(&ctx);
    shake128_inc_squeeze(output, outlen, &ctx);
    shake128_inc_ctx_release(&ctx);
}

void shake256(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen)
{
    shake256incctx ctx;

    shake256_inc_init(&ctx);
    shake256_inc_absorb(&ctx, input, inlen);
    shake256_inc_finalize(&ctx);
    shake256_inc_squeeze(output, outlen, &ctx);
    shake256_inc_ctx_release(&ctx);
}

void shake128_inc_init(shake128incctx *state)
{
    keccak_inc_reset((hesia_keccak_inc_ctx *)state);
}

void shake128_inc_absorb(shake128incctx *state,
                         const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb((hesia_keccak_inc_ctx *)state, SHAKE128_RATE, input, inlen);
}

void shake128_inc_finalize(shake128incctx *state)
{
    keccak_inc_finalize((hesia_keccak_inc_ctx *)state, SHAKE128_RATE, 0x1Fu);
}

void shake128_inc_squeeze(uint8_t *output, size_t outlen,
                          shake128incctx *state)
{
    keccak_inc_squeeze(output, outlen, (hesia_keccak_inc_ctx *)state,
                       SHAKE128_RATE);
}

void shake128_inc_ctx_release(shake128incctx *state)
{
    if (state != NULL) {
        secure_zero_memory(state, sizeof(*state));
    }
}

void shake256_inc_init(shake256incctx *state)
{
    keccak_inc_reset((hesia_keccak_inc_ctx *)state);
}

void shake256_inc_absorb(shake256incctx *state,
                         const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb((hesia_keccak_inc_ctx *)state, SHAKE256_RATE, input, inlen);
}

void shake256_inc_finalize(shake256incctx *state)
{
    keccak_inc_finalize((hesia_keccak_inc_ctx *)state, SHAKE256_RATE, 0x1Fu);
}

void shake256_inc_squeeze(uint8_t *output, size_t outlen,
                          shake256incctx *state)
{
    keccak_inc_squeeze(output, outlen, (hesia_keccak_inc_ctx *)state,
                       SHAKE256_RATE);
}

void shake256_inc_ctx_release(shake256incctx *state)
{
    if (state != NULL) {
        secure_zero_memory(state, sizeof(*state));
    }
}
