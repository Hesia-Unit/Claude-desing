#include <ta_hesia_mldsa_backend.h>
#include <tee_internal_api_extensions.h>

/*
 * Vendored ML-DSA-87 reference backend from liboqs / mldsa-native.
 * The TA uses the portable reference path with SHAKE implemented locally
 * inside the secure world, so the ML-DSA secret key never leaves OP-TEE.
 */

extern int PQCP_MLDSA_NATIVE_MLDSA87_C_signature(uint8_t *sig, size_t *siglen,
                                                 const uint8_t *m, size_t mlen,
                                                 const uint8_t *ctx, size_t ctxlen,
                                                 const uint8_t *sk);
extern int PQCP_MLDSA_NATIVE_MLDSA87_C_keypair(uint8_t *pk, uint8_t *sk);
extern int PQCP_MLDSA_NATIVE_MLDSA87_C_verify(const uint8_t *sig, size_t siglen,
                                              const uint8_t *m, size_t mlen,
                                              const uint8_t *ctx, size_t ctxlen,
                                              const uint8_t *pk);
extern int PQCP_MLDSA_NATIVE_MLDSA87_C_pk_from_sk(uint8_t *pk,
                                                  const uint8_t *sk);

static TEE_Result map_mldsa_rc(int rc)
{
    switch (rc) {
    case 0:
        return TEE_SUCCESS;
    case -2:
        return TEE_ERROR_OUT_OF_MEMORY;
    case -3:
        return TEE_ERROR_NO_DATA;
    case -1:
    default:
        return TEE_ERROR_SECURITY;
    }
}

bool hesia_ta_mldsa_backend_ready(void)
{
    return true;
}

TEE_Result hesia_ta_mldsa87_generate_keypair(uint8_t *public_key, size_t *public_key_len,
                                             uint8_t *secret_key, size_t *secret_key_len)
{
    int rc = 0;

    if (!public_key || !public_key_len || !secret_key || !secret_key_len) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (*public_key_len < HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES) {
        *public_key_len = HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES;
        return TEE_ERROR_SHORT_BUFFER;
    }
    if (*secret_key_len < HESIA_TA_MLDSA87_SECRET_KEY_BYTES) {
        *secret_key_len = HESIA_TA_MLDSA87_SECRET_KEY_BYTES;
        return TEE_ERROR_SHORT_BUFFER;
    }

    rc = PQCP_MLDSA_NATIVE_MLDSA87_C_keypair(public_key, secret_key);
    if (rc != 0) {
        EMSG("ML-DSA backend keypair failed: rc=%d", rc);
        *public_key_len = 0;
        *secret_key_len = 0;
        return map_mldsa_rc(rc);
    }

    *public_key_len = HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES;
    *secret_key_len = HESIA_TA_MLDSA87_SECRET_KEY_BYTES;
    return TEE_SUCCESS;
}

TEE_Result hesia_ta_mldsa87_sign(const uint8_t *secret_key, size_t secret_key_len,
                                 const uint8_t *message, size_t message_len,
                                 uint8_t *signature, size_t *signature_len)
{
    size_t produced = 0;
    int rc = 0;

    if (!secret_key || !message || !signature || !signature_len) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (secret_key_len != HESIA_TA_MLDSA87_SECRET_KEY_BYTES) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (*signature_len < HESIA_TA_MLDSA87_SIGNATURE_BYTES) {
        *signature_len = HESIA_TA_MLDSA87_SIGNATURE_BYTES;
        return TEE_ERROR_SHORT_BUFFER;
    }

    produced = *signature_len;
    rc = PQCP_MLDSA_NATIVE_MLDSA87_C_signature(signature, &produced,
                                               message, message_len,
                                               NULL, 0, secret_key);
    if (rc != 0) {
        EMSG("ML-DSA backend sign failed: rc=%d", rc);
        *signature_len = 0;
        return map_mldsa_rc(rc);
    }
    *signature_len = produced;
    return TEE_SUCCESS;
}

TEE_Result hesia_ta_mldsa87_verify(const uint8_t *public_key, size_t public_key_len,
                                   const uint8_t *message, size_t message_len,
                                   const uint8_t *signature, size_t signature_len)
{
    int rc = 0;

    if (!public_key || !message || !signature) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (public_key_len != HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (signature_len != HESIA_TA_MLDSA87_SIGNATURE_BYTES) {
        return TEE_ERROR_BAD_PARAMETERS;
    }

    rc = PQCP_MLDSA_NATIVE_MLDSA87_C_verify(signature, signature_len,
                                            message, message_len,
                                            NULL, 0, public_key);
    if (rc != 0) {
        EMSG("ML-DSA backend verify failed: rc=%d", rc);
    }
    return map_mldsa_rc(rc);
}

TEE_Result hesia_ta_mldsa87_public_from_secret(const uint8_t *secret_key,
                                               size_t secret_key_len,
                                               uint8_t *public_key,
                                               size_t *public_key_len)
{
    int rc = 0;

    if (!secret_key || !public_key || !public_key_len) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (secret_key_len != HESIA_TA_MLDSA87_SECRET_KEY_BYTES) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (*public_key_len < HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES) {
        *public_key_len = HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES;
        return TEE_ERROR_SHORT_BUFFER;
    }

    rc = PQCP_MLDSA_NATIVE_MLDSA87_C_pk_from_sk(public_key, secret_key);
    if (rc != 0) {
        EMSG("ML-DSA backend pk_from_sk failed: rc=%d", rc);
        *public_key_len = 0;
        return map_mldsa_rc(rc);
    }
    *public_key_len = HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES;
    return TEE_SUCCESS;
}
