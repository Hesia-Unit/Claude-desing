#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <ta_hesia.h>

#define TA_UUID TA_HESIA_UUID

#define TA_FLAGS (TA_FLAG_SINGLE_INSTANCE | TA_FLAG_INSTANCE_KEEP_ALIVE)

/*
 * ML-DSA-87 inside the TA requires substantially more secure-world heap than
 * the original sealing-only skeleton. Keep the stack modest, but provide a
 * realistic private heap budget for key generation, signing, and storage I/O.
 */
#define TA_STACK_SIZE (64 * 1024)
#define TA_DATA_SIZE  (1024 * 1024)

#define TA_VERSION "1.0"
#define TA_DESCRIPTION "HESIA sealing TA"

#endif /* USER_TA_HEADER_DEFINES_H */
