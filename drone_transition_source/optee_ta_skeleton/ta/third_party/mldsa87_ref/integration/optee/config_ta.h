/*
 * HESIA OP-TEE integration for the vendored mldsa-native ML-DSA-87
 * reference implementation.
 */

#ifndef HESIA_TA_MLDSA87_CONFIG_TA_H
#define HESIA_TA_MLDSA87_CONFIG_TA_H

#include <stddef.h>
#include <stdint.h>
#include <tee_internal_api.h>
#include "../../mldsa/src/sys.h"

#define MLD_CONFIG_PARAMETER_SET 87
#define MLD_CONFIG_NAMESPACE_PREFIX PQCP_MLDSA_NATIVE_MLDSA87_C

/* Keep the TA backend strictly portable and independent from REE libraries. */
#define MLD_CONFIG_NO_ASM
#define MLD_CONFIG_REDUCE_RAM
#define MLD_CONFIG_SERIAL_FIPS202_ONLY
#define MLD_CONFIG_FIPS202_CUSTOM_HEADER "../../integration/optee/fips202_glue.h"

#define MLD_CONFIG_CUSTOM_ZEROIZE
static MLD_INLINE void mld_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

#define MLD_CONFIG_CUSTOM_ALLOC_FREE
#define MLD_CUSTOM_ALLOC(v, T, N) \
    T *v = (T *)TEE_Malloc(sizeof(T) * (N), TEE_MALLOC_FILL_ZERO)
#define MLD_CUSTOM_FREE(v, T, N) \
    TEE_Free((void *)(v))

#define MLD_CONFIG_CUSTOM_RANDOMBYTES
static MLD_INLINE int mld_randombytes(uint8_t *ptr, size_t len)
{
    if (ptr == NULL && len != 0) {
        return -1;
    }
    if (len != 0) {
        TEE_GenerateRandom(ptr, len);
    }
    return 0;
}

#endif /* HESIA_TA_MLDSA87_CONFIG_TA_H */
