#ifndef TA_HESIA_MLDSA_BACKEND_H
#define TA_HESIA_MLDSA_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <tee_internal_api.h>

#define HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES 2592u
#define HESIA_TA_MLDSA87_SECRET_KEY_BYTES 4896u
#define HESIA_TA_MLDSA87_SIGNATURE_BYTES 4627u

bool hesia_ta_mldsa_backend_ready(void);
TEE_Result hesia_ta_mldsa87_generate_keypair(uint8_t *public_key, size_t *public_key_len,
                                             uint8_t *secret_key, size_t *secret_key_len);
TEE_Result hesia_ta_mldsa87_sign(const uint8_t *secret_key, size_t secret_key_len,
                                 const uint8_t *message, size_t message_len,
                                 uint8_t *signature, size_t *signature_len);
TEE_Result hesia_ta_mldsa87_verify(const uint8_t *public_key, size_t public_key_len,
                                   const uint8_t *message, size_t message_len,
                                   const uint8_t *signature, size_t signature_len);
TEE_Result hesia_ta_mldsa87_public_from_secret(const uint8_t *secret_key,
                                               size_t secret_key_len,
                                               uint8_t *public_key,
                                               size_t *public_key_len);

#endif /* TA_HESIA_MLDSA_BACKEND_H */
