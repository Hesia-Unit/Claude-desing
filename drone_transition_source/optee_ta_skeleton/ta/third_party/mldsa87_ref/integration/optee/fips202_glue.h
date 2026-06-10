/*
 * Glue layer matching the API names expected by mldsa-native.
 */

#ifndef HESIA_TA_MLDSA87_FIPS202_GLUE_H
#define HESIA_TA_MLDSA87_FIPS202_GLUE_H

#include "fips202.h"

#define mld_shake128ctx shake128incctx
#define mld_shake128_init shake128_inc_init
#define mld_shake128_absorb shake128_inc_absorb
#define mld_shake128_finalize shake128_inc_finalize
#define mld_shake128_squeeze shake128_inc_squeeze
#define mld_shake128_release shake128_inc_ctx_release

#define mld_shake256ctx shake256incctx
#define mld_shake256_init shake256_inc_init
#define mld_shake256_absorb shake256_inc_absorb
#define mld_shake256_finalize shake256_inc_finalize
#define mld_shake256_squeeze shake256_inc_squeeze
#define mld_shake256_release shake256_inc_ctx_release

#define mld_shake128 shake128
#define mld_shake256 shake256

#endif /* HESIA_TA_MLDSA87_FIPS202_GLUE_H */
