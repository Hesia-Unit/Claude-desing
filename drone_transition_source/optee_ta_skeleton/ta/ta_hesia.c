/*
 * OP-TEE TA - HESIA
 * Seals/unseals sensitive data using an internal AES-256-GCM key
 * stored as a persistent object in TEE secure storage.
 *
 * Dangerous maintenance commands are disabled by default. Re-enable them only
 * in controlled recovery builds by defining HESIA_TA_ENABLE_MAINTENANCE_CMDS.
 */

#include <stddef.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <ta_hesia.h>
#include <ta_hesia_mldsa_backend.h>

#define HESIA_KEY_ID        "hesia_master_key_v1"
#define HESIA_KEY_ID_LEN    20

#define HESIA_ATTEST_KEY_ID     "hesia_attest_p256_v1"
#define HESIA_ATTEST_KEY_ID_LEN (sizeof(HESIA_ATTEST_KEY_ID) - 1)
#define HESIA_ATTEST_PRIV_ID     "hesia_attest_p256_priv_v2"
#define HESIA_ATTEST_PRIV_ID_LEN (sizeof(HESIA_ATTEST_PRIV_ID) - 1)
#define HESIA_ATTEST_PUB_ID     "hesia_attest_p256_pub_v1"
#define HESIA_ATTEST_PUB_ID_LEN (sizeof(HESIA_ATTEST_PUB_ID) - 1)
#define HESIA_ATTEST_STATE_ID     "hesia_attest_state_v2"
#define HESIA_ATTEST_STATE_ID_LEN (sizeof(HESIA_ATTEST_STATE_ID) - 1)
#define HESIA_ATTEST_STATE_VERSION 2u

#define HESIA_SESSION_AUTH_ID     "hesia_session_auth_v1"
#define HESIA_SESSION_AUTH_ID_LEN (sizeof(HESIA_SESSION_AUTH_ID) - 1)
#define HESIA_SESSION_AUTH_LEN 32
#define HESIA_SESSION_AUTH_HASH_LEN 32

#define HESIA_MLDSA_KEY_ID     "hesia_mldsa87_keyblob_v1"
#define HESIA_MLDSA_KEY_ID_LEN (sizeof(HESIA_MLDSA_KEY_ID) - 1)
#define HESIA_MLDSA_SERVER_KEY_ID     "hesia_mldsa87_server_keyblob_v1"
#define HESIA_MLDSA_SERVER_KEY_ID_LEN (sizeof(HESIA_MLDSA_SERVER_KEY_ID) - 1)
#define HESIA_MLDSA_MAX_KEY_BLOB_LEN \
    (12u + HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES + HESIA_TA_MLDSA87_SECRET_KEY_BYTES)
#define HESIA_MLDSA_MAX_PUBKEY_LEN HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES
#define HESIA_MLDSA_MAX_SECRET_LEN HESIA_TA_MLDSA87_SECRET_KEY_BYTES
#define HESIA_MLDSA_MAX_SIG_LEN HESIA_TA_MLDSA87_SIGNATURE_BYTES

#define HESIA_FW_VERSION_ID     "hesia_fw_version_v4"
#define HESIA_FW_VERSION_ID_LEN (sizeof(HESIA_FW_VERSION_ID) - 1)

#define HESIA_SLOT_META_ID     "hesia_slot_meta_v1"
#define HESIA_SLOT_META_ID_LEN (sizeof(HESIA_SLOT_META_ID) - 1)

#define HESIA_RECOVERY_NONCE_ID     "hesia_recovery_state_v2"
#define HESIA_RECOVERY_NONCE_ID_LEN (sizeof(HESIA_RECOVERY_NONCE_ID) - 1)

#define HESIA_MAGIC_0 'H'
#define HESIA_MAGIC_1 'E'
#define HESIA_MAGIC_2 'S'
#define HESIA_MAGIC_3 '1'
#define HESIA_VERSION 1

#define HESIA_SLOT_REQ_MAGIC  0x48535531u
#define HESIA_SLOT_META_MAGIC 0x48534D31u
#define HESIA_SLOT_META_VERSION 1u
#define HESIA_SLOT_ID_A 1u
#define HESIA_SLOT_ID_B 2u

#define HESIA_IV_LEN  12
#define HESIA_TAG_LEN 16
#define HESIA_HDR_LEN (4 + 1 + HESIA_IV_LEN + HESIA_TAG_LEN)

#define HESIA_SESSION_AUTH_AAD "HESIA|TA|session_auth|v1"
#define HESIA_SESSION_AUTH_AAD_LEN (sizeof(HESIA_SESSION_AUTH_AAD) - 1)
#define HESIA_SESSION_AUTH_AAD_LEGACY "optee_session_auth.sealed"
#define HESIA_SESSION_AUTH_AAD_LEGACY_LEN (sizeof(HESIA_SESSION_AUTH_AAD_LEGACY) - 1)
#define HESIA_MLDSA_DRONE_AAD "HESIA|TA|mldsa_drone|v1"
#define HESIA_MLDSA_DRONE_AAD_LEN (sizeof(HESIA_MLDSA_DRONE_AAD) - 1)
#define HESIA_MLDSA_DRONE_AAD_LEGACY "mldsa-drone-slot"
#define HESIA_MLDSA_DRONE_AAD_LEGACY_LEN (sizeof(HESIA_MLDSA_DRONE_AAD_LEGACY) - 1)
#define HESIA_MLDSA_SERVER_AAD "HESIA|TA|mldsa_server|v1"
#define HESIA_MLDSA_SERVER_AAD_LEN (sizeof(HESIA_MLDSA_SERVER_AAD) - 1)
#define HESIA_MLDSA_SERVER_AAD_LEGACY "mldsa-server-slot"
#define HESIA_MLDSA_SERVER_AAD_LEGACY_LEN (sizeof(HESIA_MLDSA_SERVER_AAD_LEGACY) - 1)

static const uint8_t kHesiaRecoveryPubkey[HESIA_RECOVERY_ATTEST_PUBKEY_LEN] = {
    0x04, 0xe6, 0x90, 0xf6, 0x4f, 0xee, 0x4e, 0x7a, 0x02, 0xae, 0xa7,
    0x91, 0xf0, 0xed, 0xaa, 0x73, 0xee, 0xe6, 0xa9, 0x61, 0x72, 0x60,
    0x42, 0xd0, 0x09, 0x33, 0x62, 0xa6, 0xf3, 0xfb, 0xfc, 0x03, 0xc4,
    0x8f, 0xe2, 0x41, 0x78, 0xdc, 0x3e, 0x37, 0xf7, 0xae, 0xf6, 0x4d,
    0x2c, 0x6b, 0x44, 0xf4, 0x3d, 0xa9, 0xf1, 0x2b, 0x77, 0x4a, 0x7b,
    0x58, 0xa2, 0xd2, 0x26, 0x69, 0xa0, 0xca, 0x26, 0xa6, 0x77,
};

#define HESIA_SESS_CTX_AUTH     ((void *)0x48455349)
#define HESIA_SESS_CTX_RECOVERY ((void *)0x4845534AU)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t initialized;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t reserved;
    uint64_t max_firmware_version;
    uint64_t slot_a_firmware_version;
    uint64_t slot_b_firmware_version;
    uint64_t slot_a_asset_version;
    uint64_t slot_b_asset_version;
    uint64_t pending_firmware_version;
    uint64_t pending_asset_version;
} hesia_slot_meta_t;

typedef struct {
    uint32_t magic;
    uint8_t slot;
    uint8_t reserved0;
    uint16_t reserved1;
    uint64_t firmware_version;
    uint64_t asset_version;
} hesia_slot_request_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t expires_at_sec;
    uint8_t nonce[HESIA_RECOVERY_NONCE_LEN];
} hesia_recovery_state_t;

#define HESIA_RECOVERY_STATE_MAGIC   0x48525331u
#define HESIA_RECOVERY_STATE_VERSION 1u

static TEE_Result get_or_create_aes_key(TEE_ObjectHandle *key)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_KEY_ID,
                                              HESIA_KEY_ID_LEN,
                                              flags, &obj);
    if (res == TEE_SUCCESS) {
        *key = obj;
        return TEE_SUCCESS;
    }

    if (res != TEE_ERROR_ITEM_NOT_FOUND)
        return res;

    res = TEE_AllocateTransientObject(TEE_TYPE_AES, 256, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_GenerateKey(obj, 256, NULL, 0);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        return res;
    }

    TEE_ObjectHandle persistent = TEE_HANDLE_NULL;
    res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                     HESIA_KEY_ID,
                                     HESIA_KEY_ID_LEN,
                                     flags, obj,
                                     NULL, 0,
                                     &persistent);
    TEE_FreeTransientObject(obj);
    if (res != TEE_SUCCESS)
        return res;

    *key = persistent;
    return TEE_SUCCESS;
}

static void copy_be_padded(uint8_t *dst, size_t dst_len,
                           const uint8_t *src, size_t src_len)
{
    TEE_MemFill(dst, 0, dst_len);
    if (src_len > dst_len) {
        src += src_len - dst_len;
        src_len = dst_len;
    }
    if (src_len > 0) {
        TEE_MemMove(dst + (dst_len - src_len), src, src_len);
    }
}

static TEE_Result export_attest_pubkey_from_handle(TEE_ObjectHandle key,
                                                   uint8_t out[HESIA_RECOVERY_ATTEST_PUBKEY_LEN])
{
    uint8_t x[66];
    uint8_t y[66];
    size_t x_len = sizeof(x);
    size_t y_len = sizeof(y);
    TEE_Result res;

    res = TEE_GetObjectBufferAttribute(key, TEE_ATTR_ECC_PUBLIC_VALUE_X,
                                       x, &x_len);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_GetObjectBufferAttribute(key, TEE_ATTR_ECC_PUBLIC_VALUE_Y,
                                       y, &y_len);
    if (res != TEE_SUCCESS)
        return res;

    out[0] = 0x04;
    copy_be_padded(out + 1, 32, x, x_len);
    copy_be_padded(out + 33, 32, y, y_len);
    return TEE_SUCCESS;
}

static TEE_Result read_attest_pubkey_blob(
    uint8_t out[HESIA_RECOVERY_ATTEST_PUBKEY_LEN])
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_ATTEST_PUB_ID,
                                              HESIA_ATTEST_PUB_ID_LEN,
                                              flags, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, out, HESIA_RECOVERY_ATTEST_PUBKEY_LEN,
                             &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != HESIA_RECOVERY_ATTEST_PUBKEY_LEN)
        return TEE_ERROR_SECURITY;
    return TEE_SUCCESS;
}

static TEE_Result read_attest_private_blob(uint8_t out[32], size_t *out_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;
    TEE_Result res;

    if (out_len == NULL || *out_len < 32)
        return TEE_ERROR_SHORT_BUFFER;

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_PRIV_ID,
                                   HESIA_ATTEST_PRIV_ID_LEN,
                                   flags, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, out, 32, &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != 32)
        return TEE_ERROR_SECURITY;
    *out_len = read_count;
    return TEE_SUCCESS;
}

static TEE_Result write_attest_state_version(uint32_t version)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint8_t encoded[4];
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res;

    encoded[0] = (uint8_t)(version & 0xffu);
    encoded[1] = (uint8_t)((version >> 8) & 0xffu);
    encoded[2] = (uint8_t)((version >> 16) & 0xffu);
    encoded[3] = (uint8_t)((version >> 24) & 0xffu);

    res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                     HESIA_ATTEST_STATE_ID,
                                     HESIA_ATTEST_STATE_ID_LEN,
                                     flags,
                                     TEE_HANDLE_NULL,
                                     encoded,
                                     sizeof(encoded),
                                     &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    return res;
}

static TEE_Result read_attest_state_version(uint32_t *version)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint8_t encoded[4];
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;
    TEE_Result res;

    if (version == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_STATE_ID,
                                   HESIA_ATTEST_STATE_ID_LEN,
                                   flags,
                                   &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, encoded, sizeof(encoded), &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != sizeof(encoded))
        return TEE_ERROR_SECURITY;

    *version = (uint32_t)encoded[0] |
               ((uint32_t)encoded[1] << 8) |
               ((uint32_t)encoded[2] << 16) |
               ((uint32_t)encoded[3] << 24);
    return TEE_SUCCESS;
}

static TEE_Result write_attest_pubkey_blob(
    const uint8_t pub[HESIA_RECOVERY_ATTEST_PUBKEY_LEN])
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_ATTEST_PUB_ID,
                                                HESIA_ATTEST_PUB_ID_LEN,
                                                flags,
                                                TEE_HANDLE_NULL,
                                                (void *)pub,
                                                HESIA_RECOVERY_ATTEST_PUBKEY_LEN,
                                                &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    return res;
}

static TEE_Result write_attest_private_blob(const uint8_t priv[32], size_t priv_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res;

    if (priv_len != 32)
        return TEE_ERROR_BAD_PARAMETERS;

    res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                     HESIA_ATTEST_PRIV_ID,
                                     HESIA_ATTEST_PRIV_ID_LEN,
                                     flags,
                                     TEE_HANDLE_NULL,
                                     (void *)priv,
                                     priv_len,
                                     &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    return res;
}

static TEE_Result delete_attest_material_if_present(void)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_ATTEST_KEY_ID,
                                              HESIA_ATTEST_KEY_ID_LEN,
                                              flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_PRIV_ID,
                                   HESIA_ATTEST_PRIV_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_PUB_ID,
                                   HESIA_ATTEST_PUB_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    }
    if (res != TEE_ERROR_ITEM_NOT_FOUND)
        return res;

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_STATE_ID,
                                   HESIA_ATTEST_STATE_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    }
    if (res == TEE_ERROR_ITEM_NOT_FOUND)
        return TEE_SUCCESS;
    return res;
}

static TEE_Result load_attest_keypair_from_blobs(TEE_ObjectHandle *key)
{
    uint8_t pubkey[HESIA_RECOVERY_ATTEST_PUBKEY_LEN];
    uint8_t priv[32];
    size_t priv_len = sizeof(priv);
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    TEE_Attribute attrs[4];
    TEE_Result res;

    if (key == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    res = read_attest_pubkey_blob(pubkey);
    if (res != TEE_SUCCESS)
        return res;
    res = read_attest_private_blob(priv, &priv_len);
    if (res != TEE_SUCCESS) {
        TEE_MemFill(pubkey, 0, sizeof(pubkey));
        return res;
    }

    res = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_KEYPAIR, 256, &obj);
    if (res != TEE_SUCCESS) {
        TEE_MemFill(pubkey, 0, sizeof(pubkey));
        TEE_MemFill(priv, 0, sizeof(priv));
        return res;
    }

    TEE_InitRefAttribute(&attrs[0], TEE_ATTR_ECC_PRIVATE_VALUE, priv, priv_len);
    TEE_InitRefAttribute(&attrs[1], TEE_ATTR_ECC_PUBLIC_VALUE_X, pubkey + 1, 32);
    TEE_InitRefAttribute(&attrs[2], TEE_ATTR_ECC_PUBLIC_VALUE_Y, pubkey + 33, 32);
    TEE_InitValueAttribute(&attrs[3], TEE_ATTR_ECC_CURVE, TEE_ECC_CURVE_NIST_P256, 0);

    res = TEE_PopulateTransientObject(obj, attrs, 4);
    TEE_MemFill(pubkey, 0, sizeof(pubkey));
    TEE_MemFill(priv, 0, sizeof(priv));
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        return res;
    }

    *key = obj;
    return TEE_SUCCESS;
}

static TEE_Result create_and_persist_attest_key(TEE_ObjectHandle *key)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint8_t pubkey[HESIA_RECOVERY_ATTEST_PUBKEY_LEN];
    uint8_t priv[66];
    size_t priv_len = sizeof(priv);
    TEE_Result res;

    res = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_KEYPAIR, 256, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_GenerateKey(obj, 256, NULL, 0);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        return res;
    }

    res = export_attest_pubkey_from_handle(obj, pubkey);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        return res;
    }

    res = TEE_GetObjectBufferAttribute(obj, TEE_ATTR_ECC_PRIVATE_VALUE, priv, &priv_len);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        TEE_MemFill(pubkey, 0, sizeof(pubkey));
        return res;
    }

    res = delete_attest_material_if_present();
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(obj);
        TEE_MemFill(pubkey, 0, sizeof(pubkey));
        TEE_MemFill(priv, 0, sizeof(priv));
        return res;
    }

    res = write_attest_private_blob(priv, priv_len);
    TEE_MemFill(priv, 0, sizeof(priv));
    if (res != TEE_SUCCESS) {
        TEE_MemFill(pubkey, 0, sizeof(pubkey));
        TEE_FreeTransientObject(obj);
        return res;
    }

    res = write_attest_pubkey_blob(pubkey);
    TEE_MemFill(pubkey, 0, sizeof(pubkey));
    if (res != TEE_SUCCESS) {
        delete_attest_material_if_present();
        TEE_FreeTransientObject(obj);
        return res;
    }

    res = write_attest_state_version(HESIA_ATTEST_STATE_VERSION);
    if (res != TEE_SUCCESS) {
        delete_attest_material_if_present();
        TEE_FreeTransientObject(obj);
        return res;
    }

    *key = obj;
    return TEE_SUCCESS;
}

static TEE_Result get_or_create_attest_key(TEE_ObjectHandle *key)
{
    uint32_t state_version = 0;
    TEE_Result state_res = read_attest_state_version(&state_version);
    TEE_Result res;

    if (state_res == TEE_SUCCESS && state_version == HESIA_ATTEST_STATE_VERSION) {
        res = load_attest_keypair_from_blobs(key);
        if (res == TEE_SUCCESS)
            return TEE_SUCCESS;
    }

    return create_and_persist_attest_key(key);
}

static TEE_Result get_attest_pubkey_bytes(uint8_t out[HESIA_RECOVERY_ATTEST_PUBKEY_LEN])
{
    TEE_Result res = read_attest_pubkey_blob(out);
    TEE_ObjectHandle key = TEE_HANDLE_NULL;

    if (res == TEE_SUCCESS)
        return TEE_SUCCESS;

    res = get_or_create_attest_key(&key);
    if (res != TEE_SUCCESS)
        return res;
    TEE_CloseObject(key);
    return read_attest_pubkey_blob(out);
}

static TEE_Result get_recovery_time_seconds(uint64_t *out)
{
    TEE_Time now = { 0 };

    if (out == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_GetSystemTime(&now);
    *out = now.seconds;
    return TEE_SUCCESS;
}

static TEE_Result write_recovery_nonce(
    const uint8_t nonce[HESIA_RECOVERY_NONCE_LEN],
    uint64_t expires_at_sec)
{
    hesia_recovery_state_t state;
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;

    TEE_MemFill(&state, 0, sizeof(state));
    state.magic = HESIA_RECOVERY_STATE_MAGIC;
    state.version = HESIA_RECOVERY_STATE_VERSION;
    state.expires_at_sec = expires_at_sec;
    TEE_MemMove(state.nonce, nonce, HESIA_RECOVERY_NONCE_LEN);

    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_RECOVERY_NONCE_ID,
                                                HESIA_RECOVERY_NONCE_ID_LEN,
                                                flags, TEE_HANDLE_NULL,
                                                &state,
                                                sizeof(state),
                                                &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    TEE_MemFill(&state, 0, sizeof(state));
    return res;
}

static TEE_Result read_recovery_nonce(
    uint8_t nonce[HESIA_RECOVERY_NONCE_LEN],
    uint64_t *expires_at_sec)
{
    hesia_recovery_state_t state;
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;

    if (expires_at_sec == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_MemFill(&state, 0, sizeof(state));
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_RECOVERY_NONCE_ID,
                                              HESIA_RECOVERY_NONCE_ID_LEN,
                                              flags, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, &state, sizeof(state),
                             &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != sizeof(state) ||
        state.magic != HESIA_RECOVERY_STATE_MAGIC ||
        state.version != HESIA_RECOVERY_STATE_VERSION) {
        TEE_MemFill(&state, 0, sizeof(state));
        return TEE_ERROR_SECURITY;
    }
    TEE_MemMove(nonce, state.nonce, HESIA_RECOVERY_NONCE_LEN);
    *expires_at_sec = state.expires_at_sec;
    TEE_MemFill(&state, 0, sizeof(state));
    return TEE_SUCCESS;
}

static void clear_recovery_nonce(void)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_RECOVERY_NONCE_ID,
                                              HESIA_RECOVERY_NONCE_ID_LEN,
                                              flags, &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseAndDeletePersistentObject1(obj);
}

static TEE_Result wipe_key_internal(void)
{
    TEE_Result res;
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_KEY_ID,
                                   HESIA_KEY_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_KEY_ID,
                                   HESIA_ATTEST_KEY_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_ATTEST_PUB_ID,
                                   HESIA_ATTEST_PUB_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_SESSION_AUTH_ID,
                                   HESIA_SESSION_AUTH_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_MLDSA_KEY_ID,
                                   HESIA_MLDSA_KEY_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_MLDSA_SERVER_KEY_ID,
                                   HESIA_MLDSA_SERVER_KEY_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_RECOVERY_NONCE_ID,
                                   HESIA_RECOVERY_NONCE_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_SLOT_META_ID,
                                   HESIA_SLOT_META_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    } else if (res != TEE_ERROR_ITEM_NOT_FOUND) {
        return res;
    }

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_FW_VERSION_ID,
                                   HESIA_FW_VERSION_ID_LEN,
                                   flags, &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseAndDeletePersistentObject1(obj);
        obj = TEE_HANDLE_NULL;
    }
    if (res == TEE_ERROR_ITEM_NOT_FOUND)
        return TEE_SUCCESS;
    return res;
}

static TEE_Result rotate_key_internal(void)
{
    TEE_Result res = wipe_key_internal();
    if (res != TEE_SUCCESS)
        return res;

    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    res = get_or_create_aes_key(&key);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(key);
    if (res != TEE_SUCCESS)
        return res;

    key = TEE_HANDLE_NULL;
    res = get_or_create_attest_key(&key);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(key);
    return res;
}

static TEE_Result wipe_key_cmd(uint32_t param_types)
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    return wipe_key_internal();
}

static TEE_Result rotate_key_cmd(uint32_t param_types)
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    return rotate_key_internal();
}

static bool slot_id_is_valid(uint8_t slot)
{
    return slot == HESIA_SLOT_ID_A || slot == HESIA_SLOT_ID_B;
}

static void slot_meta_init_default(hesia_slot_meta_t *meta)
{
    TEE_MemFill(meta, 0, sizeof(*meta));
    meta->magic = HESIA_SLOT_META_MAGIC;
    meta->version = HESIA_SLOT_META_VERSION;
}

static TEE_Result read_slot_meta(hesia_slot_meta_t *meta)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;
    TEE_Result res;

    slot_meta_init_default(meta);

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                   HESIA_SLOT_META_ID,
                                   HESIA_SLOT_META_ID_LEN,
                                   flags, &obj);
    if (res == TEE_ERROR_ITEM_NOT_FOUND)
        return TEE_SUCCESS;
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, meta, sizeof(*meta), &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != sizeof(*meta))
        return TEE_ERROR_SECURITY;
    if (meta->magic != HESIA_SLOT_META_MAGIC || meta->version != HESIA_SLOT_META_VERSION)
        return TEE_ERROR_SECURITY;
    return TEE_SUCCESS;
}

static TEE_Result write_slot_meta(const hesia_slot_meta_t *meta)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_SLOT_META_ID,
                                                HESIA_SLOT_META_ID_LEN,
                                                flags, TEE_HANDLE_NULL,
                                                meta, sizeof(*meta),
                                                &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    return res;
}

static uint64_t *slot_fw_version_ptr(hesia_slot_meta_t *meta, uint8_t slot)
{
    return (slot == HESIA_SLOT_ID_A) ? &meta->slot_a_firmware_version : &meta->slot_b_firmware_version;
}

static uint64_t *slot_asset_version_ptr(hesia_slot_meta_t *meta, uint8_t slot)
{
    return (slot == HESIA_SLOT_ID_A) ? &meta->slot_a_asset_version : &meta->slot_b_asset_version;
}

static const uint64_t *slot_fw_version_ptr_const(const hesia_slot_meta_t *meta, uint8_t slot)
{
    return (slot == HESIA_SLOT_ID_A) ? &meta->slot_a_firmware_version : &meta->slot_b_firmware_version;
}

static const uint64_t *slot_asset_version_ptr_const(const hesia_slot_meta_t *meta, uint8_t slot)
{
    return (slot == HESIA_SLOT_ID_A) ? &meta->slot_a_asset_version : &meta->slot_b_asset_version;
}

static TEE_Result parse_slot_request(uint32_t param_types,
                                     TEE_Param params[4],
                                     hesia_slot_request_t *req)
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.buffer == NULL || params[0].memref.size != sizeof(*req))
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_MemMove(req, params[0].memref.buffer, sizeof(*req));
    if (req->magic != HESIA_SLOT_REQ_MAGIC)
        return TEE_ERROR_BAD_PARAMETERS;
    if (!slot_id_is_valid(req->slot))
        return TEE_ERROR_BAD_PARAMETERS;
    if (req->firmware_version == 0 || req->asset_version == 0)
        return TEE_ERROR_BAD_PARAMETERS;
    return TEE_SUCCESS;
}

static TEE_Result parse_mldsa_key_blob(const uint8_t *blob, size_t blob_len,
                                       const uint8_t **pubkey, size_t *pubkey_len,
                                       const uint8_t **secret_key, size_t *secret_key_len)
{
    uint32_t pk_len = 0;
    uint32_t sk_len = 0;
    size_t expected = 0;

    if (!blob || blob_len < 12 || !pubkey || !pubkey_len || !secret_key || !secret_key_len)
        return TEE_ERROR_BAD_PARAMETERS;
    if (!(blob[0] == 'H' && blob[1] == 'D' && blob[2] == 'K' && blob[3] == '1'))
        return TEE_ERROR_BAD_PARAMETERS;

    pk_len = ((uint32_t)blob[4] << 24) |
             ((uint32_t)blob[5] << 16) |
             ((uint32_t)blob[6] << 8) |
             (uint32_t)blob[7];
    sk_len = ((uint32_t)blob[8] << 24) |
             ((uint32_t)blob[9] << 16) |
             ((uint32_t)blob[10] << 8) |
             (uint32_t)blob[11];

    expected = 12u + (size_t)pk_len + (size_t)sk_len;
    if (blob_len != expected ||
        pk_len != HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES ||
        sk_len != HESIA_TA_MLDSA87_SECRET_KEY_BYTES) {
        return TEE_ERROR_BAD_PARAMETERS;
    }

    *pubkey = blob + 12;
    *pubkey_len = pk_len;
    *secret_key = blob + 12 + pk_len;
    *secret_key_len = sk_len;
    return TEE_SUCCESS;
}

static void store_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static void secure_free_buffer(uint8_t *buf, size_t len)
{
    if (buf != NULL) {
        if (len != 0) {
            TEE_MemFill(buf, 0, len);
        }
        TEE_Free(buf);
    }
}

static TEE_Result serialize_mldsa_key_blob(uint8_t *blob, size_t *blob_len,
                                           const uint8_t *pubkey, size_t pubkey_len,
                                           const uint8_t *secret_key, size_t secret_key_len)
{
    const size_t expected = 12u + pubkey_len + secret_key_len;

    if (!blob || !blob_len || !pubkey || !secret_key)
        return TEE_ERROR_BAD_PARAMETERS;
    if (pubkey_len != HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES ||
        secret_key_len != HESIA_TA_MLDSA87_SECRET_KEY_BYTES)
        return TEE_ERROR_BAD_PARAMETERS;
    if (*blob_len < expected) {
        *blob_len = expected;
        return TEE_ERROR_SHORT_BUFFER;
    }

    blob[0] = 'H';
    blob[1] = 'D';
    blob[2] = 'K';
    blob[3] = '1';
    store_u32_be(blob + 4, (uint32_t)pubkey_len);
    store_u32_be(blob + 8, (uint32_t)secret_key_len);
    TEE_MemMove(blob + 12, pubkey, pubkey_len);
    TEE_MemMove(blob + 12 + pubkey_len, secret_key, secret_key_len);
    *blob_len = expected;
    return TEE_SUCCESS;
}

static TEE_Result read_persistent_blob(const char *id, size_t id_len,
                                       uint8_t *out, size_t *out_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    TEE_ObjectInfo info;
    uint32_t obj_size = 0;
    size_t read_count = 0;
    TEE_Result res;

    if (!id || !out_len)
        return TEE_ERROR_BAD_PARAMETERS;

    res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE, id, id_len, flags, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_GetObjectInfo1(obj, &info);
    if (res != TEE_SUCCESS) {
        TEE_CloseObject(obj);
        return res;
    }
    obj_size = info.dataSize;

    if (*out_len < obj_size) {
        *out_len = obj_size;
        TEE_CloseObject(obj);
        return TEE_ERROR_SHORT_BUFFER;
    }

    res = TEE_ReadObjectData(obj, out, obj_size, &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != obj_size)
        return TEE_ERROR_SECURITY;
    *out_len = read_count;
    return TEE_SUCCESS;
}

static TEE_Result write_persistent_blob(const char *id, size_t id_len,
                                        const uint8_t *data, size_t data_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                id, id_len,
                                                flags, TEE_HANDLE_NULL,
                                                (void *)data, data_len,
                                                &obj);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(obj);
    return res;
}

static TEE_Result delete_persistent_blob_if_present(const char *id, size_t id_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              id, id_len,
                                              TEE_DATA_FLAG_ACCESS_READ |
                                              TEE_DATA_FLAG_ACCESS_WRITE |
                                              TEE_DATA_FLAG_ACCESS_WRITE_META,
                                              &obj);
    if (res == TEE_ERROR_ITEM_NOT_FOUND) {
        return TEE_SUCCESS;
    }
    if (res != TEE_SUCCESS) {
        return res;
    }
    TEE_CloseAndDeletePersistentObject1(obj);
    return TEE_SUCCESS;
}

static TEE_Result resolve_mldsa_slot_identity(uint32_t slot_value,
                                              const char **id,
                                              size_t *id_len)
{
    if (id == NULL || id_len == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    switch (slot_value) {
    case HESIA_MLDSA_SLOT_DRONE:
        *id = HESIA_MLDSA_KEY_ID;
        *id_len = HESIA_MLDSA_KEY_ID_LEN;
        return TEE_SUCCESS;
    case HESIA_MLDSA_SLOT_SERVER:
        *id = HESIA_MLDSA_SERVER_KEY_ID;
        *id_len = HESIA_MLDSA_SERVER_KEY_ID_LEN;
        return TEE_SUCCESS;
    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}

static bool mldsa_key_blob_present_for_slot(const char *id, size_t id_len)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              id,
                                              id_len,
                                              TEE_DATA_FLAG_ACCESS_READ,
                                              &obj);
    if (res == TEE_SUCCESS) {
        TEE_CloseObject(obj);
        return true;
    }
    return false;
}

#ifndef TEE_ALG_SHA3_512
#error "OP-TEE missing SHA3-512 support required for HKDF"
#endif

#define HESIA_SHA3_512_DIGEST_LEN 64
#define HESIA_SHA3_512_BLOCK_LEN 72

static void write_u64_be(uint8_t out[8], uint64_t v)
{
    for (int i = 7; i >= 0; --i) {
        out[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

static uint64_t read_u64_be(const uint8_t in[8])
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | in[i];
    }
    return v;
}

static bool constant_time_equals(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    size_t i;

    for (i = 0; i < len; ++i)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static TEE_Result sha256_bytes(const uint8_t *in, size_t in_len,
                               uint8_t out[HESIA_SESSION_AUTH_HASH_LEN])
{
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    size_t out_len = HESIA_SESSION_AUTH_HASH_LEN;
    TEE_Result res = TEE_AllocateOperation(&op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_DigestDoFinal(op, in, in_len, out, &out_len);
    TEE_FreeOperation(op);
    if (res != TEE_SUCCESS)
        return res;
    if (out_len != HESIA_SESSION_AUTH_HASH_LEN)
        return TEE_ERROR_SECURITY;
    return TEE_SUCCESS;
}

static TEE_Result verify_recovery_signature(
    const uint8_t digest[HESIA_SESSION_AUTH_HASH_LEN],
    const uint8_t signature[HESIA_RECOVERY_SIG_LEN])
{
    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    TEE_Attribute attrs[3];
    TEE_Result res;

    res = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_PUBLIC_KEY, 256, &key);
    if (res != TEE_SUCCESS)
        return res;

    TEE_InitRefAttribute(&attrs[0], TEE_ATTR_ECC_PUBLIC_VALUE_X,
                         (void *)(kHesiaRecoveryPubkey + 1), 32);
    TEE_InitRefAttribute(&attrs[1], TEE_ATTR_ECC_PUBLIC_VALUE_Y,
                         (void *)(kHesiaRecoveryPubkey + 33), 32);
    TEE_InitValueAttribute(&attrs[2], TEE_ATTR_ECC_CURVE,
                           TEE_ECC_CURVE_NIST_P256, 0);

    res = TEE_PopulateTransientObject(key, attrs, 3);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(key);
        return res;
    }

    res = TEE_AllocateOperation(&op, TEE_ALG_ECDSA_P256, TEE_MODE_VERIFY, 256);
    if (res != TEE_SUCCESS) {
        TEE_FreeTransientObject(key);
        return res;
    }

    res = TEE_SetOperationKey(op, key);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(op);
        TEE_FreeTransientObject(key);
        return res;
    }

    res = TEE_AsymmetricVerifyDigest(op, NULL, 0,
                                     digest, HESIA_SESSION_AUTH_HASH_LEN,
                                     (void *)signature,
                                     HESIA_RECOVERY_SIG_LEN);
    TEE_FreeOperation(op);
    TEE_FreeTransientObject(key);
    return res;
}

static TEE_Result store_session_auth_hash(const uint8_t hash[HESIA_SESSION_AUTH_HASH_LEN])
{
    TEE_ObjectHandle persistent = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_SESSION_AUTH_ID,
                                                HESIA_SESSION_AUTH_ID_LEN,
                                                flags,
                                                TEE_HANDLE_NULL,
                                                hash,
                                                HESIA_SESSION_AUTH_HASH_LEN,
                                                &persistent);
    if (res == TEE_SUCCESS)
        TEE_CloseObject(persistent);
    return res;
}

static TEE_Result read_session_auth_hash(uint8_t hash[HESIA_SESSION_AUTH_HASH_LEN])
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    size_t read_count = 0;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_SESSION_AUTH_ID,
                                              HESIA_SESSION_AUTH_ID_LEN,
                                              flags, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_ReadObjectData(obj, hash, HESIA_SESSION_AUTH_HASH_LEN, &read_count);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS)
        return res;
    if (read_count != HESIA_SESSION_AUTH_HASH_LEN)
        return TEE_ERROR_SECURITY;
    return TEE_SUCCESS;
}

static TEE_Result set_session_auth_secret_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    uint8_t hash[HESIA_SESSION_AUTH_HASH_LEN];
    TEE_Result res;

    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size != 32)
        return TEE_ERROR_BAD_PARAMETERS;

    res = sha256_bytes(params[0].memref.buffer, params[0].memref.size, hash);
    if (res != TEE_SUCCESS)
        return res;
    return store_session_auth_hash(hash);
}

static bool blob_has_hesia_header(const uint8_t *in, size_t in_len)
{
    return in != NULL &&
           in_len >= HESIA_HDR_LEN &&
           in[0] == HESIA_MAGIC_0 &&
           in[1] == HESIA_MAGIC_1 &&
           in[2] == HESIA_MAGIC_2 &&
           in[3] == HESIA_MAGIC_3 &&
           in[4] == HESIA_VERSION;
}

static TEE_Result update_aad_if_present(TEE_OperationHandle op,
                                        const void *aad,
                                        size_t aad_len)
{
    if (aad == NULL || aad_len == 0)
        return TEE_SUCCESS;
    TEE_AEUpdateAAD(op, (void *)aad, aad_len);
    return TEE_SUCCESS;
}

static TEE_Result unseal_secret_blob(const uint8_t *in, size_t in_len,
                                     const void *aad, size_t aad_len,
                                     uint8_t *out, size_t *out_len)
{
    size_t cipher_len;
    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    TEE_Result res;
    const uint8_t *iv;
    const uint8_t *tag;
    const uint8_t *cipher;

    if (!blob_has_hesia_header(in, in_len))
        return TEE_ERROR_BAD_PARAMETERS;

    cipher_len = in_len - HESIA_HDR_LEN;
    if (*out_len < cipher_len) {
        *out_len = cipher_len;
        return TEE_ERROR_SHORT_BUFFER;
    }

    res = get_or_create_aes_key(&key);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_AllocateOperation(&op, TEE_ALG_AES_GCM, TEE_MODE_DECRYPT, 256);
    if (res != TEE_SUCCESS) {
        TEE_CloseObject(key);
        return res;
    }

    res = TEE_SetOperationKey(op, key);
    if (res != TEE_SUCCESS)
        goto out;

    iv = in + 5;
    tag = in + 5 + HESIA_IV_LEN;
    cipher = in + HESIA_HDR_LEN;

    res = TEE_AEInit(op, iv, HESIA_IV_LEN, HESIA_TAG_LEN * 8, aad_len, 0);
    if (res != TEE_SUCCESS)
        goto out;

    res = update_aad_if_present(op, aad, aad_len);
    if (res != TEE_SUCCESS)
        goto out;

    res = TEE_AEDecryptFinal(op, cipher, cipher_len, out, out_len,
                             (void *)tag, HESIA_TAG_LEN);

out:
    TEE_FreeOperation(op);
    TEE_CloseObject(key);
    return res;
}

static TEE_Result verify_session_auth_or_bootstrap(uint32_t param_types,
                                                   TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    uint8_t supplied_hash[HESIA_SESSION_AUTH_HASH_LEN];
    uint8_t expected_hash[HESIA_SESSION_AUTH_HASH_LEN];
    uint8_t supplied_secret[64];
    size_t supplied_secret_len = sizeof(supplied_secret);
    TEE_Result res;
    const uint8_t *auth_buf;
    size_t auth_len;

    if (param_types != exp)
        return TEE_ERROR_ACCESS_DENIED;
    if (params[0].memref.buffer == NULL || params[0].memref.size == 0)
        return TEE_ERROR_ACCESS_DENIED;

    auth_buf = (const uint8_t *)params[0].memref.buffer;
    auth_len = params[0].memref.size;

    if (auth_len == 32) {
        TEE_MemMove(supplied_secret, auth_buf, auth_len);
        supplied_secret_len = auth_len;
    } else if (blob_has_hesia_header(auth_buf, auth_len)) {
        res = unseal_secret_blob(auth_buf, auth_len,
                                 HESIA_SESSION_AUTH_AAD, HESIA_SESSION_AUTH_AAD_LEN,
                                 supplied_secret, &supplied_secret_len);
        if ((res == TEE_ERROR_MAC_INVALID || res == TEE_ERROR_BAD_PARAMETERS) &&
            auth_len > HESIA_HDR_LEN) {
            supplied_secret_len = sizeof(supplied_secret);
            res = unseal_secret_blob(auth_buf, auth_len,
                                     HESIA_SESSION_AUTH_AAD_LEGACY,
                                     HESIA_SESSION_AUTH_AAD_LEGACY_LEN,
                                     supplied_secret, &supplied_secret_len);
        }
        if (res != TEE_SUCCESS)
            return res;
    } else {
        return TEE_ERROR_ACCESS_DENIED;
    }

    if (supplied_secret_len != 32) {
        TEE_MemFill(supplied_secret, 0, sizeof(supplied_secret));
        return TEE_ERROR_ACCESS_DENIED;
    }

    res = sha256_bytes(supplied_secret, supplied_secret_len, supplied_hash);
    TEE_MemFill(supplied_secret, 0, sizeof(supplied_secret));
    if (res != TEE_SUCCESS)
        return res;

    res = read_session_auth_hash(expected_hash);
    if (res == TEE_ERROR_ITEM_NOT_FOUND) {
        return TEE_ERROR_ACCESS_DENIED;
    }
    if (res != TEE_SUCCESS)
        return res;

    return constant_time_equals(expected_hash, supplied_hash, HESIA_SESSION_AUTH_HASH_LEN)
           ? TEE_SUCCESS
           : TEE_ERROR_ACCESS_DENIED;
}

static TEE_Result hmac_sha3_512(const uint8_t* key, size_t key_len,
                                const uint8_t* msg, size_t msg_len,
                                uint8_t* out, size_t* out_len)
{
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    uint8_t key_block[HESIA_SHA3_512_BLOCK_LEN];
    uint8_t key_hash[HESIA_SHA3_512_DIGEST_LEN];
    uint8_t ipad[HESIA_SHA3_512_BLOCK_LEN];
    uint8_t opad[HESIA_SHA3_512_BLOCK_LEN];
    uint8_t inner[HESIA_SHA3_512_DIGEST_LEN];
    const uint8_t* key_material = key;
    size_t key_material_len = key_len;
    size_t inner_len = sizeof(inner);
    TEE_Result res;

    if (*out_len < HESIA_SHA3_512_DIGEST_LEN)
        return TEE_ERROR_SHORT_BUFFER;

    if (key_material_len > HESIA_SHA3_512_BLOCK_LEN) {
        size_t hashed_len = sizeof(key_hash);
        res = TEE_AllocateOperation(&op, TEE_ALG_SHA3_512, TEE_MODE_DIGEST, 0);
        if (res != TEE_SUCCESS)
            return res;
        res = TEE_DigestDoFinal(op, key, key_len, key_hash, &hashed_len);
        TEE_FreeOperation(op);
        op = TEE_HANDLE_NULL;
        if (res != TEE_SUCCESS)
            return res;
        if (hashed_len != HESIA_SHA3_512_DIGEST_LEN)
            return TEE_ERROR_SECURITY;
        key_material = key_hash;
        key_material_len = hashed_len;
    }

    TEE_MemFill(key_block, 0, sizeof(key_block));
    if (key_material_len > 0)
        TEE_MemMove(key_block, key_material, key_material_len);

    for (size_t i = 0; i < HESIA_SHA3_512_BLOCK_LEN; ++i) {
        ipad[i] = (uint8_t)(key_block[i] ^ 0x36);
        opad[i] = (uint8_t)(key_block[i] ^ 0x5c);
    }

    res = TEE_AllocateOperation(&op, TEE_ALG_SHA3_512, TEE_MODE_DIGEST, 0);
    if (res != TEE_SUCCESS)
        goto cleanup;
    TEE_DigestUpdate(op, ipad, sizeof(ipad));
    res = TEE_DigestDoFinal(op, msg, msg_len, inner, &inner_len);
    TEE_FreeOperation(op);
    op = TEE_HANDLE_NULL;
    if (res != TEE_SUCCESS)
        goto cleanup;
    if (inner_len != HESIA_SHA3_512_DIGEST_LEN) {
        res = TEE_ERROR_SECURITY;
        goto cleanup;
    }

    *out_len = HESIA_SHA3_512_DIGEST_LEN;
    res = TEE_AllocateOperation(&op, TEE_ALG_SHA3_512, TEE_MODE_DIGEST, 0);
    if (res != TEE_SUCCESS)
        goto cleanup;
    TEE_DigestUpdate(op, opad, sizeof(opad));
    res = TEE_DigestDoFinal(op, inner, inner_len, out, out_len);

cleanup:
    if (op != TEE_HANDLE_NULL)
        TEE_FreeOperation(op);
    TEE_MemFill(key_block, 0, sizeof(key_block));
    TEE_MemFill(key_hash, 0, sizeof(key_hash));
    TEE_MemFill(ipad, 0, sizeof(ipad));
    TEE_MemFill(opad, 0, sizeof(opad));
    TEE_MemFill(inner, 0, sizeof(inner));

    return res;
}

static TEE_Result hkdf_sha3_512(const uint8_t* ikm, size_t ikm_len,
                                const uint8_t* salt, size_t salt_len,
                                const uint8_t* info, size_t info_len,
                                uint8_t* out, size_t* out_len)
{
    uint8_t prk[64];
    uint8_t t[64];
    size_t prk_len = sizeof(prk);
    size_t t_len = sizeof(t);
    TEE_Result res;

    uint8_t zero_salt[64] = {0};
    const uint8_t* salt_ptr = salt_len ? salt : zero_salt;
    size_t salt_use = salt_len ? salt_len : sizeof(zero_salt);

    res = hmac_sha3_512(salt_ptr, salt_use, ikm, ikm_len, prk, &prk_len);
    if (res != TEE_SUCCESS)
        return res;

    uint8_t info_buf[256];
    size_t total = 0;
    if (info_len > sizeof(info_buf) - 1)
        return TEE_ERROR_BAD_PARAMETERS;
    if (info_len > 0) {
        TEE_MemMove(info_buf, info, info_len);
        total = info_len;
    }
    info_buf[total++] = 0x01;

    res = hmac_sha3_512(prk, prk_len, info_buf, total, t, &t_len);
    if (res != TEE_SUCCESS)
        return res;

    if (*out_len > t_len)
        return TEE_ERROR_SHORT_BUFFER;
    TEE_MemMove(out, t, *out_len);
    return TEE_SUCCESS;
}

static TEE_Result hkdf_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

    size_t out_len = params[3].memref.size;
    TEE_Result res = hkdf_sha3_512(params[0].memref.buffer, params[0].memref.size,
                                   params[1].memref.buffer, params[1].memref.size,
                                   params[2].memref.buffer, params[2].memref.size,
                                   params[3].memref.buffer, &out_len);
    if (res == TEE_ERROR_SHORT_BUFFER) {
        params[3].memref.size = out_len;
        return res;
    }
    params[3].memref.size = out_len;
    return res;
}

static TEE_Result export_attest_pubkey_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size < HESIA_RECOVERY_ATTEST_PUBKEY_LEN) {
        params[0].memref.size = HESIA_RECOVERY_ATTEST_PUBKEY_LEN;
        return TEE_ERROR_SHORT_BUFFER;
    }
    params[0].memref.size = HESIA_RECOVERY_ATTEST_PUBKEY_LEN;
    return get_attest_pubkey_bytes((uint8_t *)params[0].memref.buffer);
}

static TEE_Result sign_attest_digest_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size != 32)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[1].memref.size < 64) {
        params[1].memref.size = 64;
        return TEE_ERROR_SHORT_BUFFER;
    }

    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_Result res = get_or_create_attest_key(&key);
    if (res != TEE_SUCCESS)
        return res;

    TEE_OperationHandle op = TEE_HANDLE_NULL;
    res = TEE_AllocateOperation(&op, TEE_ALG_ECDSA_P256, TEE_MODE_SIGN, 256);
    if (res != TEE_SUCCESS) {
        TEE_CloseObject(key);
        return res;
    }

    res = TEE_SetOperationKey(op, key);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(op);
        TEE_CloseObject(key);
        return res;
    }

    size_t sig_len = params[1].memref.size;
    res = TEE_AsymmetricSignDigest(op,
                                   NULL, 0,
                                   params[0].memref.buffer,
                                   params[0].memref.size,
                                   params[1].memref.buffer,
                                   &sig_len);
    params[1].memref.size = sig_len;

    TEE_FreeOperation(op);
    TEE_CloseObject(key);
    return res;
}

static TEE_Result import_mldsa_key_blob_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_VALUE_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                              TEE_PARAM_TYPE_VALUE_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_INPUT,
                                              TEE_PARAM_TYPE_NONE);
    const uint32_t exp_value_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                                     TEE_PARAM_TYPE_VALUE_INOUT,
                                                     TEE_PARAM_TYPE_NONE,
                                                     TEE_PARAM_TYPE_NONE);
    const uint32_t exp_value_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                                          TEE_PARAM_TYPE_VALUE_INOUT,
                                                          TEE_PARAM_TYPE_VALUE_INPUT,
                                                          TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                      TEE_PARAM_TYPE_VALUE_OUTPUT,
                                                      TEE_PARAM_TYPE_NONE,
                                                      TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                           TEE_PARAM_TYPE_VALUE_OUTPUT,
                                                           TEE_PARAM_TYPE_VALUE_INPUT,
                                                           TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_value_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                            TEE_PARAM_TYPE_VALUE_INOUT,
                                                            TEE_PARAM_TYPE_NONE,
                                                            TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_value_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                                 TEE_PARAM_TYPE_VALUE_INOUT,
                                                                 TEE_PARAM_TYPE_VALUE_INPUT,
                                                                 TEE_PARAM_TYPE_NONE);
    const uint32_t exp_legacy = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                                TEE_PARAM_TYPE_NONE,
                                                TEE_PARAM_TYPE_NONE,
                                                TEE_PARAM_TYPE_NONE);
    static const uint8_t kImportSelfTestMessage[] = "HESIA-MLDSA-IMPORT-PCT-v1";
    uint8_t *blob = NULL;
    uint8_t *signature = NULL;
    const size_t blob_cap = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    const size_t signature_cap = HESIA_TA_MLDSA87_SIGNATURE_BYTES;
    const size_t sealed_blob_len = params[0].memref.size;
    size_t blob_len = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    size_t signature_len = HESIA_TA_MLDSA87_SIGNATURE_BYTES;
    const uint8_t *pubkey = NULL;
    const uint8_t *secret_key = NULL;
    const void *aad = HESIA_MLDSA_DRONE_AAD;
    size_t aad_len = HESIA_MLDSA_DRONE_AAD_LEN;
    const void *legacy_aad = HESIA_MLDSA_DRONE_AAD_LEGACY;
    size_t legacy_aad_len = HESIA_MLDSA_DRONE_AAD_LEGACY_LEN;
    size_t pubkey_len = 0;
    size_t secret_key_len = 0;
    const char *key_id = HESIA_MLDSA_KEY_ID;
    size_t key_id_len = HESIA_MLDSA_KEY_ID_LEN;
    TEE_Result res;
    const bool has_status_param = (param_types == exp ||
                                   param_types == exp_slot ||
                                   param_types == exp_value_inout ||
                                   param_types == exp_value_inout_slot ||
                                   param_types == exp_memref_inout ||
                                   param_types == exp_memref_inout_slot ||
                                   param_types == exp_memref_value_inout ||
                                   param_types == exp_memref_value_inout_slot);

    if (!(has_status_param || param_types == exp_legacy))
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.buffer == NULL || params[0].memref.size == 0)
        return TEE_ERROR_BAD_PARAMETERS;
    if (has_status_param) {
        params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_INIT;
        params[1].value.b = TEE_SUCCESS;
    }
    if (param_types == exp_slot ||
        param_types == exp_value_inout_slot ||
        param_types == exp_memref_inout_slot ||
        param_types == exp_memref_value_inout_slot) {
        res = resolve_mldsa_slot_identity(params[2].value.a, &key_id, &key_id_len);
        if (res != TEE_SUCCESS) {
            if (has_status_param) {
                params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_INIT;
                params[1].value.b = res;
            }
            return res;
        }
        if (params[2].value.a == HESIA_MLDSA_SLOT_SERVER) {
            aad = HESIA_MLDSA_SERVER_AAD;
            aad_len = HESIA_MLDSA_SERVER_AAD_LEN;
            legacy_aad = HESIA_MLDSA_SERVER_AAD_LEGACY;
            legacy_aad_len = HESIA_MLDSA_SERVER_AAD_LEGACY_LEN;
        }
    }
    IMSG("ML-DSA import invoked with sealed blob length %zu", sealed_blob_len);

    blob = TEE_Malloc(blob_cap, TEE_MALLOC_FILL_ZERO);
    signature = TEE_Malloc(signature_cap, TEE_MALLOC_FILL_ZERO);
    if (blob == NULL || signature == NULL) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_ALLOC;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import allocation failure: res=0x%x", res);
        goto out;
    }

    res = unseal_secret_blob((const uint8_t *)params[0].memref.buffer,
                             sealed_blob_len,
                             aad, aad_len,
                             blob, &blob_len);
    if ((res == TEE_ERROR_MAC_INVALID || res == TEE_ERROR_BAD_PARAMETERS) &&
        sealed_blob_len > HESIA_HDR_LEN) {
        blob_len = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
        res = unseal_secret_blob((const uint8_t *)params[0].memref.buffer,
                                 sealed_blob_len,
                                 legacy_aad, legacy_aad_len,
                                 blob, &blob_len);
    }
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_UNSEAL;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import unseal failed: res=0x%x", res);
        goto out;
    }

    res = parse_mldsa_key_blob(blob, blob_len, &pubkey, &pubkey_len,
                               &secret_key, &secret_key_len);
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_PARSE;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import parse failed: res=0x%x", res);
        goto out;
    }

    if (!hesia_ta_mldsa_backend_ready()) {
        res = TEE_ERROR_NOT_SUPPORTED;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_BACKEND_READY;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import backend unavailable: res=0x%x", res);
        goto out;
    }

    /*
     * Pairwise consistency test for imported keys:
     * sign a fixed challenge inside the TA, then verify it against the
     * supplied public key. This keeps validation inside secure world and
     * tolerates backend-specific secret-key encodings as long as the pair is
     * cryptographically coherent.
     */
    res = hesia_ta_mldsa87_sign(secret_key, secret_key_len,
                                kImportSelfTestMessage,
                                sizeof(kImportSelfTestMessage) - 1u,
                                signature, &signature_len);
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_SELFTEST_SIGN;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import self-test sign failed: res=0x%x", res);
        goto out;
    }
    res = hesia_ta_mldsa87_verify(pubkey, pubkey_len,
                                  kImportSelfTestMessage,
                                  sizeof(kImportSelfTestMessage) - 1u,
                                  signature, signature_len);
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_SELFTEST_VERIFY;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import self-test verify failed: res=0x%x", res);
        goto out;
    }

    res = delete_persistent_blob_if_present(key_id, key_id_len);
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_PERSIST;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import delete-before-persist failed: res=0x%x", res);
        goto out;
    }

    res = write_persistent_blob(key_id, key_id_len,
                                blob, blob_len);
    if (res == TEE_ERROR_GENERIC) {
        res = TEE_ERROR_STORAGE_NOT_AVAILABLE;
    }
    if (res != TEE_SUCCESS) {
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_PERSIST;
            params[1].value.b = res;
        }
        EMSG("ML-DSA import persist failed: res=0x%x", res);
        goto out;
    }
    if (has_status_param) {
        params[1].value.a = HESIA_MLDSA_IMPORT_STAGE_DONE;
        params[1].value.b = TEE_SUCCESS;
    }
    IMSG("ML-DSA import completed successfully");

out:
    secure_free_buffer(signature, signature_cap);
    secure_free_buffer(blob, blob_cap);
    (void)pubkey;
    (void)pubkey_len;
    (void)secret_key;
    (void)secret_key_len;
    return has_status_param ? TEE_SUCCESS : res;
}

static TEE_Result export_mldsa_pubkey_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_INPUT,
                                              TEE_PARAM_TYPE_NONE,
                                              TEE_PARAM_TYPE_NONE);
    uint8_t *blob = NULL;
    const size_t blob_cap = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    size_t blob_len = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    const uint8_t *pubkey = NULL;
    const uint8_t *secret_key = NULL;
    size_t pubkey_len = 0;
    size_t secret_key_len = 0;
    const char *key_id = HESIA_MLDSA_KEY_ID;
    size_t key_id_len = HESIA_MLDSA_KEY_ID_LEN;
    TEE_Result res;

    if (param_types != exp && param_types != exp_slot)
        return TEE_ERROR_BAD_PARAMETERS;
    if (param_types == exp_slot) {
        res = resolve_mldsa_slot_identity(params[1].value.a, &key_id, &key_id_len);
        if (res != TEE_SUCCESS)
            return res;
    }

    blob = TEE_Malloc(blob_cap, TEE_MALLOC_FILL_ZERO);
    if (blob == NULL)
        return TEE_ERROR_OUT_OF_MEMORY;

    res = read_persistent_blob(key_id, key_id_len,
                               blob, &blob_len);
    if (res != TEE_SUCCESS)
        goto out;

    res = parse_mldsa_key_blob(blob, blob_len, &pubkey, &pubkey_len,
                               &secret_key, &secret_key_len);
    if (res != TEE_SUCCESS)
        goto out;
    if (params[0].memref.size < pubkey_len) {
        params[0].memref.size = pubkey_len;
        res = TEE_ERROR_SHORT_BUFFER;
        goto out;
    }

    TEE_MemMove(params[0].memref.buffer, pubkey, pubkey_len);
    params[0].memref.size = pubkey_len;
    res = TEE_SUCCESS;

out:
    secure_free_buffer(blob, blob_cap);
    (void)secret_key;
    (void)secret_key_len;
    return res;
}

static TEE_Result sign_mldsa_payload_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                              TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_INPUT,
                                              TEE_PARAM_TYPE_NONE);
    uint8_t *blob = NULL;
    const size_t blob_cap = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    size_t blob_len = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    const uint8_t *pubkey = NULL;
    const uint8_t *secret_key = NULL;
    size_t pubkey_len = 0;
    size_t secret_key_len = 0;
    size_t sig_len = params[1].memref.size;
    const char *key_id = HESIA_MLDSA_KEY_ID;
    size_t key_id_len = HESIA_MLDSA_KEY_ID_LEN;
    TEE_Result res;

    if (param_types != exp && param_types != exp_slot)
        return TEE_ERROR_BAD_PARAMETERS;
    if (param_types == exp_slot) {
        res = resolve_mldsa_slot_identity(params[2].value.a, &key_id, &key_id_len);
        if (res != TEE_SUCCESS)
            return res;
    }
    if (!hesia_ta_mldsa_backend_ready())
        return TEE_ERROR_NOT_SUPPORTED;
    if (params[0].memref.buffer == NULL || params[0].memref.size == 0 ||
        params[0].memref.size > 1024 * 1024)
        return TEE_ERROR_BAD_PARAMETERS;

    blob = TEE_Malloc(blob_cap, TEE_MALLOC_FILL_ZERO);
    if (blob == NULL)
        return TEE_ERROR_OUT_OF_MEMORY;

    res = read_persistent_blob(key_id, key_id_len,
                               blob, &blob_len);
    if (res != TEE_SUCCESS)
        goto out;

    res = parse_mldsa_key_blob(blob, blob_len, &pubkey, &pubkey_len,
                               &secret_key, &secret_key_len);
    if (res != TEE_SUCCESS)
        goto out;

    if (params[1].memref.buffer == NULL) {
        params[1].memref.size = HESIA_MLDSA_MAX_SIG_LEN;
        res = TEE_ERROR_SHORT_BUFFER;
        goto out;
    }

    res = hesia_ta_mldsa87_sign(secret_key, secret_key_len,
                                (const uint8_t *)params[0].memref.buffer,
                                params[0].memref.size,
                                (uint8_t *)params[1].memref.buffer,
                                &sig_len);
    params[1].memref.size = sig_len;

out:
    secure_free_buffer(blob, blob_cap);
    (void)pubkey;
    (void)pubkey_len;
    return res;
}

static TEE_Result get_mldsa_status_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_INPUT,
                                              TEE_PARAM_TYPE_NONE,
                                              TEE_PARAM_TYPE_NONE);
    const char *key_id = HESIA_MLDSA_KEY_ID;
    size_t key_id_len = HESIA_MLDSA_KEY_ID_LEN;
    TEE_Result res;
    if (param_types != exp && param_types != exp_slot)
        return TEE_ERROR_BAD_PARAMETERS;
    if (param_types == exp_slot) {
        res = resolve_mldsa_slot_identity(params[1].value.a, &key_id, &key_id_len);
        if (res != TEE_SUCCESS)
            return res;
    }
    params[0].value.a = hesia_ta_mldsa_backend_ready() ? 1u : 0u;
    params[0].value.b = mldsa_key_blob_present_for_slot(key_id, key_id_len) ? 1u : 0u;
    return TEE_SUCCESS;
}

static TEE_Result generate_mldsa_keypair_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_VALUE_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_OUTPUT,
                                              TEE_PARAM_TYPE_VALUE_INPUT,
                                              TEE_PARAM_TYPE_NONE);
    const uint32_t exp_value_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                                     TEE_PARAM_TYPE_VALUE_INOUT,
                                                     TEE_PARAM_TYPE_NONE,
                                                     TEE_PARAM_TYPE_NONE);
    const uint32_t exp_value_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                                          TEE_PARAM_TYPE_VALUE_INOUT,
                                                          TEE_PARAM_TYPE_VALUE_INPUT,
                                                          TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                      TEE_PARAM_TYPE_VALUE_OUTPUT,
                                                      TEE_PARAM_TYPE_NONE,
                                                      TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                           TEE_PARAM_TYPE_VALUE_OUTPUT,
                                                           TEE_PARAM_TYPE_VALUE_INPUT,
                                                           TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_value_inout = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                            TEE_PARAM_TYPE_VALUE_INOUT,
                                                            TEE_PARAM_TYPE_NONE,
                                                            TEE_PARAM_TYPE_NONE);
    const uint32_t exp_memref_value_inout_slot = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                                                 TEE_PARAM_TYPE_VALUE_INOUT,
                                                                 TEE_PARAM_TYPE_VALUE_INPUT,
                                                                 TEE_PARAM_TYPE_NONE);
    const uint32_t exp_legacy = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                                TEE_PARAM_TYPE_NONE,
                                                TEE_PARAM_TYPE_NONE,
                                                TEE_PARAM_TYPE_NONE);
    uint8_t *blob = NULL;
    uint8_t *pubkey = NULL;
    uint8_t *secret_key = NULL;
    size_t blob_len = HESIA_MLDSA_MAX_KEY_BLOB_LEN;
    size_t pubkey_len = HESIA_TA_MLDSA87_PUBLIC_KEY_BYTES;
    size_t secret_key_len = HESIA_TA_MLDSA87_SECRET_KEY_BYTES;
    const char *key_id = HESIA_MLDSA_KEY_ID;
    size_t key_id_len = HESIA_MLDSA_KEY_ID_LEN;
    TEE_Result res;
    const bool has_status_param = (param_types == exp ||
                                   param_types == exp_slot ||
                                   param_types == exp_value_inout ||
                                   param_types == exp_value_inout_slot ||
                                   param_types == exp_memref_inout ||
                                   param_types == exp_memref_inout_slot ||
                                   param_types == exp_memref_value_inout ||
                                   param_types == exp_memref_value_inout_slot);

    if (!(has_status_param || param_types == exp_legacy))
        return TEE_ERROR_BAD_PARAMETERS;
    if (!hesia_ta_mldsa_backend_ready())
        return TEE_ERROR_NOT_SUPPORTED;
    params[0].memref.size = 0;
    if (has_status_param) {
        params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_INIT;
        params[1].value.b = TEE_SUCCESS;
    }
    if (param_types == exp_slot ||
        param_types == exp_value_inout_slot ||
        param_types == exp_memref_inout_slot ||
        param_types == exp_memref_value_inout_slot) {
        res = resolve_mldsa_slot_identity(params[2].value.a, &key_id, &key_id_len);
        if (res != TEE_SUCCESS) {
            if (has_status_param) {
                params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_INIT;
                params[1].value.b = res;
            }
            return res;
        }
    }
    IMSG("ML-DSA key generation invoked");

    blob = TEE_Malloc(blob_len, TEE_MALLOC_FILL_ZERO);
    pubkey = TEE_Malloc(pubkey_len, TEE_MALLOC_FILL_ZERO);
    secret_key = TEE_Malloc(secret_key_len, TEE_MALLOC_FILL_ZERO);
    if (!blob || !pubkey || !secret_key) {
        params[0].memref.size = 1;
        res = TEE_ERROR_OUT_OF_MEMORY;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_ALLOC;
            params[1].value.b = res;
        }
        EMSG("ML-DSA key generation allocation failure: res=0x%x", res);
        goto out;
    }

    res = hesia_ta_mldsa87_generate_keypair(pubkey, &pubkey_len,
                                            secret_key, &secret_key_len);
    if (res != TEE_SUCCESS) {
        params[0].memref.size = 2;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_GENERATE;
            params[1].value.b = res;
        }
        EMSG("ML-DSA key generation backend generate failed: res=0x%x", res);
        goto out;
    }
    res = serialize_mldsa_key_blob(blob, &blob_len,
                                   pubkey, pubkey_len,
                                   secret_key, secret_key_len);
    if (res != TEE_SUCCESS) {
        params[0].memref.size = 3;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_SERIALIZE;
            params[1].value.b = res;
        }
        EMSG("ML-DSA key generation serialize failed: res=0x%x", res);
        goto out;
    }
    res = delete_persistent_blob_if_present(key_id,
                                            key_id_len);
    if (res != TEE_SUCCESS) {
        params[0].memref.size = 4;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_PERSIST;
            params[1].value.b = res;
        }
        EMSG("ML-DSA key generation delete-before-persist failed: res=0x%x", res);
        goto out;
    }
    res = write_persistent_blob(key_id, key_id_len,
                                blob, blob_len);
    if (res == TEE_ERROR_GENERIC) {
        res = TEE_ERROR_STORAGE_NOT_AVAILABLE;
    }
    if (res != TEE_SUCCESS) {
        params[0].memref.size = 4;
        if (has_status_param) {
            params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_PERSIST;
            params[1].value.b = res;
        }
        EMSG("ML-DSA key generation persist failed: res=0x%x", res);
        goto out;
    }
    if (params[0].memref.size < pubkey_len) {
        params[0].memref.size = pubkey_len;
        res = TEE_ERROR_SHORT_BUFFER;
        goto out;
    }

    TEE_MemMove(params[0].memref.buffer, pubkey, pubkey_len);
    params[0].memref.size = pubkey_len;
    if (has_status_param) {
        params[1].value.a = HESIA_MLDSA_KEYGEN_STAGE_DONE;
        params[1].value.b = TEE_SUCCESS;
    }
    res = TEE_SUCCESS;
    IMSG("ML-DSA key generation completed successfully");

out:
    if (blob) {
        TEE_MemFill(blob, 0, blob_len);
        TEE_Free(blob);
    }
    if (pubkey) {
        TEE_MemFill(pubkey, 0, pubkey_len);
        TEE_Free(pubkey);
    }
    if (secret_key) {
        TEE_MemFill(secret_key, 0, secret_key_len);
        TEE_Free(secret_key);
    }
    if (!has_status_param) {
        return res;
    }
    return (res == TEE_ERROR_SHORT_BUFFER) ? res : TEE_SUCCESS;
}

static TEE_Result get_recovery_challenge_cmd(uint32_t param_types,
                                             TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    hesia_recovery_challenge_t challenge;
    uint64_t now_sec = 0;
    TEE_Result res;

    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size < sizeof(challenge)) {
        params[0].memref.size = sizeof(challenge);
        return TEE_ERROR_SHORT_BUFFER;
    }

    TEE_MemFill(&challenge, 0, sizeof(challenge));
    challenge.magic = HESIA_RECOVERY_MAGIC;
    challenge.version = HESIA_RECOVERY_VERSION;
    res = get_recovery_time_seconds(&now_sec);
    if (res != TEE_SUCCESS)
        return res;
    challenge.expires_at_sec = now_sec + HESIA_RECOVERY_TTL_SEC;
    TEE_GenerateRandom(challenge.nonce, sizeof(challenge.nonce));
    res = get_attest_pubkey_bytes(challenge.attest_pubkey);
    if (res != TEE_SUCCESS)
        return res;
    res = write_recovery_nonce(challenge.nonce, challenge.expires_at_sec);
    if (res != TEE_SUCCESS) {
        TEE_MemFill(&challenge, 0, sizeof(challenge));
        return res;
    }
    TEE_MemMove(params[0].memref.buffer, &challenge, sizeof(challenge));
    params[0].memref.size = sizeof(challenge);
    TEE_MemFill(&challenge, 0, sizeof(challenge));
    return TEE_SUCCESS;
}

static TEE_Result recover_session_auth_cmd(uint32_t param_types,
                                           TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    hesia_recovery_token_t token;
    uint8_t expected_nonce[HESIA_RECOVERY_NONCE_LEN];
    uint64_t expected_expires_at_sec = 0;
    uint8_t expected_hash[HESIA_SESSION_AUTH_HASH_LEN];
    uint8_t current_attest_pubkey[HESIA_RECOVERY_ATTEST_PUBKEY_LEN];
    uint8_t current_attest_pubkey_hash[HESIA_SESSION_AUTH_HASH_LEN];
    uint8_t token_digest[HESIA_SESSION_AUTH_HASH_LEN];
    uint64_t now_sec = 0;
    TEE_Result res;

    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.buffer == NULL || params[1].memref.buffer == NULL)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size != sizeof(token))
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[1].memref.size != HESIA_SESSION_AUTH_LEN)
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_MemMove(&token, params[0].memref.buffer, sizeof(token));
    if (token.magic != HESIA_RECOVERY_MAGIC ||
        token.version != HESIA_RECOVERY_VERSION)
        return TEE_ERROR_ACCESS_DENIED;
    if (token.expires_at_sec == 0)
        return TEE_ERROR_ACCESS_DENIED;

    res = read_recovery_nonce(expected_nonce, &expected_expires_at_sec);
    if (res != TEE_SUCCESS)
        return TEE_ERROR_ACCESS_DENIED;
    if (!constant_time_equals(expected_nonce, token.nonce,
                              HESIA_RECOVERY_NONCE_LEN)) {
        TEE_MemFill(expected_nonce, 0, sizeof(expected_nonce));
        return TEE_ERROR_ACCESS_DENIED;
    }
    if (expected_expires_at_sec != token.expires_at_sec) {
        TEE_MemFill(expected_nonce, 0, sizeof(expected_nonce));
        return TEE_ERROR_ACCESS_DENIED;
    }
    TEE_MemFill(expected_nonce, 0, sizeof(expected_nonce));

    res = get_recovery_time_seconds(&now_sec);
    if (res != TEE_SUCCESS)
        return res;
    if (now_sec > token.expires_at_sec) {
        clear_recovery_nonce();
        return TEE_ERROR_ACCESS_DENIED;
    }

    res = sha256_bytes(params[1].memref.buffer, params[1].memref.size,
                       expected_hash);
    if (res != TEE_SUCCESS)
        return res;
    if (!constant_time_equals(expected_hash, token.new_secret_hash,
                              HESIA_SESSION_AUTH_HASH_LEN))
        return TEE_ERROR_ACCESS_DENIED;

    res = get_attest_pubkey_bytes(current_attest_pubkey);
    if (res != TEE_SUCCESS)
        return res;
    res = sha256_bytes(current_attest_pubkey, sizeof(current_attest_pubkey),
                       current_attest_pubkey_hash);
    TEE_MemFill(current_attest_pubkey, 0, sizeof(current_attest_pubkey));
    if (res != TEE_SUCCESS)
        return res;
    if (!constant_time_equals(current_attest_pubkey_hash,
                              token.attest_pubkey_hash,
                              HESIA_SESSION_AUTH_HASH_LEN)) {
        TEE_MemFill(current_attest_pubkey_hash, 0,
                    sizeof(current_attest_pubkey_hash));
        return TEE_ERROR_ACCESS_DENIED;
    }
    TEE_MemFill(current_attest_pubkey_hash, 0,
                sizeof(current_attest_pubkey_hash));

    res = sha256_bytes((const uint8_t *)&token,
                       offsetof(hesia_recovery_token_t, signature),
                       token_digest);
    if (res != TEE_SUCCESS)
        return res;

    res = verify_recovery_signature(token_digest, token.signature);
    if (res != TEE_SUCCESS)
        return TEE_ERROR_ACCESS_DENIED;

    res = store_session_auth_hash(expected_hash);
    TEE_MemFill(expected_hash, 0, sizeof(expected_hash));
    TEE_MemFill(token_digest, 0, sizeof(token_digest));
    if (res == TEE_SUCCESS)
        clear_recovery_nonce();
    return res;
}

static TEE_Result read_fw_version(uint64_t *out)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_FW_VERSION_ID,
                                              HESIA_FW_VERSION_ID_LEN,
                                              flags, &obj);
    if (res == TEE_ERROR_ITEM_NOT_FOUND) {
        *out = 0;
        return TEE_SUCCESS;
    }
    if (res == TEE_ERROR_ACCESS_DENIED) {
        // Legacy object created without READ permission; treat as missing
        *out = 0;
        return TEE_SUCCESS;
    }
    if (res != TEE_SUCCESS)
        return res;

    uint8_t buf[8];
    size_t read_bytes = 0;
    res = TEE_ReadObjectData(obj, buf, sizeof(buf), &read_bytes);
    TEE_CloseObject(obj);
    if (res != TEE_SUCCESS || read_bytes != sizeof(buf))
        return TEE_ERROR_BAD_PARAMETERS;
    *out = read_u64_be(buf);
    return TEE_SUCCESS;
}

static TEE_Result write_fw_version(uint64_t v)
{
    uint8_t buf[8];
    write_u64_be(buf, v);

    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_FW_VERSION_ID,
                                                HESIA_FW_VERSION_ID_LEN,
                                                flags, TEE_HANDLE_NULL,
                                                buf, sizeof(buf),
                                                &obj);
    if (res == TEE_ERROR_ACCESS_CONFLICT) {
        // Legacy object may lack READ flag: delete & recreate with full flags.
        uint32_t del_flags = TEE_DATA_FLAG_ACCESS_WRITE |
                             TEE_DATA_FLAG_ACCESS_WRITE_META;
        res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                       HESIA_FW_VERSION_ID,
                                       HESIA_FW_VERSION_ID_LEN,
                                       del_flags, &obj);
        if (res == TEE_SUCCESS) {
            (void)TEE_CloseAndDeletePersistentObject1(obj);
            obj = TEE_HANDLE_NULL;
        }
        res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                         HESIA_FW_VERSION_ID,
                                         HESIA_FW_VERSION_ID_LEN,
                                         flags, TEE_HANDLE_NULL,
                                         buf, sizeof(buf),
                                         &obj);
    }
    if (obj != TEE_HANDLE_NULL)
        TEE_CloseObject(obj);
    return res;
}

static TEE_Result check_version_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

    uint64_t incoming = ((uint64_t)params[0].value.a << 32) | params[0].value.b;
    uint64_t current = 0;
    TEE_Result res = read_fw_version(&current);
    if (res != TEE_SUCCESS)
        return res;
    if (incoming < current)
        return TEE_ERROR_SECURITY;
    if (incoming > current)
        return write_fw_version(incoming);
    return TEE_SUCCESS;
}

static TEE_Result reset_version_cmd(uint32_t param_types, TEE_Param params[4])
{
    (void)params;
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

    uint8_t buf[8];
    write_u64_be(buf, 0);

    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META |
                     TEE_DATA_FLAG_OVERWRITE;
    TEE_Result res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE,
                                                HESIA_FW_VERSION_ID,
                                                HESIA_FW_VERSION_ID_LEN,
                                                flags, TEE_HANDLE_NULL,
                                                NULL, 0, &obj);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_WriteObjectData(obj, buf, sizeof(buf));
    TEE_CloseObject(obj);
    return res;
}

static TEE_Result read_version_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

    uint64_t current = 0;
    TEE_Result res = read_fw_version(&current);
    if (res != TEE_SUCCESS)
        return res;
    params[0].value.a = (uint32_t)(current >> 32);
    params[0].value.b = (uint32_t)(current & 0xFFFFFFFFu);
    return TEE_SUCCESS;
}

static TEE_Result stage_slot_update_cmd(uint32_t param_types, TEE_Param params[4])
{
    hesia_slot_request_t req;
    hesia_slot_meta_t meta;
    TEE_Result res = parse_slot_request(param_types, params, &req);
    if (res != TEE_SUCCESS)
        return res;

    res = read_slot_meta(&meta);
    if (res != TEE_SUCCESS)
        return res;

    if (meta.initialized && meta.pending_slot != 0)
        return TEE_ERROR_ACCESS_CONFLICT;
    if (meta.initialized && req.slot == meta.active_slot)
        return TEE_ERROR_ACCESS_CONFLICT;
    if (meta.initialized && req.firmware_version < meta.max_firmware_version)
        return TEE_ERROR_SECURITY;
    if (meta.initialized && req.firmware_version < *slot_fw_version_ptr_const(&meta, req.slot))
        return TEE_ERROR_SECURITY;
    if (meta.initialized && req.asset_version < *slot_asset_version_ptr_const(&meta, req.slot))
        return TEE_ERROR_SECURITY;

    meta.initialized = 1;
    meta.pending_slot = req.slot;
    meta.pending_firmware_version = req.firmware_version;
    meta.pending_asset_version = req.asset_version;
    return write_slot_meta(&meta);
}

static TEE_Result commit_slot_boot_cmd(uint32_t param_types, TEE_Param params[4])
{
    hesia_slot_request_t req;
    hesia_slot_meta_t meta;
    TEE_Result res = parse_slot_request(param_types, params, &req);
    if (res != TEE_SUCCESS)
        return res;

    res = read_slot_meta(&meta);
    if (res != TEE_SUCCESS)
        return res;

    if (!meta.initialized) {
        meta.initialized = 1;
        meta.active_slot = req.slot;
        meta.pending_slot = 0;
        meta.max_firmware_version = req.firmware_version;
        *slot_fw_version_ptr(&meta, req.slot) = req.firmware_version;
        *slot_asset_version_ptr(&meta, req.slot) = req.asset_version;
        meta.pending_firmware_version = 0;
        meta.pending_asset_version = 0;
        return write_slot_meta(&meta);
    }

    if (meta.pending_slot != 0) {
        if (req.slot != meta.pending_slot)
            return TEE_ERROR_SECURITY;
        if (req.firmware_version != meta.pending_firmware_version ||
            req.asset_version != meta.pending_asset_version)
            return TEE_ERROR_SECURITY;
    } else if (req.slot != meta.active_slot) {
        return TEE_ERROR_SECURITY;
    }

    if (req.firmware_version < meta.max_firmware_version)
        return TEE_ERROR_SECURITY;
    if (req.firmware_version < *slot_fw_version_ptr_const(&meta, req.slot))
        return TEE_ERROR_SECURITY;
    if (req.asset_version < *slot_asset_version_ptr_const(&meta, req.slot))
        return TEE_ERROR_SECURITY;

    meta.active_slot = req.slot;
    meta.pending_slot = 0;
    meta.pending_firmware_version = 0;
    meta.pending_asset_version = 0;
    if (req.firmware_version > meta.max_firmware_version)
        meta.max_firmware_version = req.firmware_version;
    *slot_fw_version_ptr(&meta, req.slot) = req.firmware_version;
    *slot_asset_version_ptr(&meta, req.slot) = req.asset_version;
    return write_slot_meta(&meta);
}

static TEE_Result read_slot_meta_cmd(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    hesia_slot_meta_t meta;
    TEE_Result res;

    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;
    if (params[0].memref.size < sizeof(meta)) {
        params[0].memref.size = sizeof(meta);
        return TEE_ERROR_SHORT_BUFFER;
    }

    res = read_slot_meta(&meta);
    if (res != TEE_SUCCESS)
        return res;

    TEE_MemMove(params[0].memref.buffer, &meta, sizeof(meta));
    params[0].memref.size = sizeof(meta);
    return TEE_SUCCESS;
}

static TEE_Result seal_secret(uint32_t param_types, TEE_Param params[4])
{
    // params[0]: in  plaintext
    // params[1]: out sealed blob
    // params[2]: optional AAD / domain label
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_aad = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                             TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                             TEE_PARAM_TYPE_MEMREF_INPUT,
                                             TEE_PARAM_TYPE_NONE);
    const void *aad = NULL;
    size_t aad_len = 0;
    if (param_types != exp && param_types != exp_aad)
        return TEE_ERROR_BAD_PARAMETERS;
    if (param_types == exp_aad) {
        aad = params[2].memref.buffer;
        aad_len = params[2].memref.size;
    }

    if (params[1].memref.size < HESIA_HDR_LEN + params[0].memref.size) {
        params[1].memref.size = HESIA_HDR_LEN + params[0].memref.size;
        return TEE_ERROR_SHORT_BUFFER;
    }

    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_Result res = get_or_create_aes_key(&key);
    if (res != TEE_SUCCESS)
        return res;

    TEE_OperationHandle op = TEE_HANDLE_NULL;
    res = TEE_AllocateOperation(&op, TEE_ALG_AES_GCM, TEE_MODE_ENCRYPT, 256);
    if (res != TEE_SUCCESS) {
        TEE_CloseObject(key);
        return res;
    }

    res = TEE_SetOperationKey(op, key);
    if (res != TEE_SUCCESS)
        goto out;

    uint8_t *out = (uint8_t *)params[1].memref.buffer;
    out[0] = HESIA_MAGIC_0;
    out[1] = HESIA_MAGIC_1;
    out[2] = HESIA_MAGIC_2;
    out[3] = HESIA_MAGIC_3;
    out[4] = HESIA_VERSION;

    uint8_t *iv = out + 5;
    uint8_t *tag = out + 5 + HESIA_IV_LEN;
    uint8_t *cipher = out + HESIA_HDR_LEN;

    TEE_GenerateRandom(iv, HESIA_IV_LEN);

    res = TEE_AEInit(op, iv, HESIA_IV_LEN, HESIA_TAG_LEN * 8, aad_len, 0);
    if (res != TEE_SUCCESS)
        goto out;

    res = update_aad_if_present(op, aad, aad_len);
    if (res != TEE_SUCCESS)
        goto out;

    size_t out_len = params[0].memref.size;
    size_t tag_len = HESIA_TAG_LEN;
    res = TEE_AEEncryptFinal(op,
                             params[0].memref.buffer, params[0].memref.size,
                             cipher, &out_len,
                             tag, &tag_len);
    if (res != TEE_SUCCESS)
        goto out;

    params[1].memref.size = HESIA_HDR_LEN + out_len;
    res = TEE_SUCCESS;

out:
    TEE_FreeOperation(op);
    TEE_CloseObject(key);
    return res;
}

static TEE_Result unseal_secret(uint32_t param_types, TEE_Param params[4])
{
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    const uint32_t exp_aad = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                             TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                             TEE_PARAM_TYPE_MEMREF_INPUT,
                                             TEE_PARAM_TYPE_NONE);
    const void *aad = NULL;
    size_t aad_len = 0;
    if (param_types != exp && param_types != exp_aad)
        return TEE_ERROR_BAD_PARAMETERS;
    if (param_types == exp_aad) {
        aad = params[2].memref.buffer;
        aad_len = params[2].memref.size;
    }

    if (params[0].memref.size < HESIA_HDR_LEN)
        return TEE_ERROR_BAD_PARAMETERS;

    const uint8_t *in = (const uint8_t *)params[0].memref.buffer;
    if (in[0] != HESIA_MAGIC_0 || in[1] != HESIA_MAGIC_1 ||
        in[2] != HESIA_MAGIC_2 || in[3] != HESIA_MAGIC_3 ||
        in[4] != HESIA_VERSION)
        return TEE_ERROR_BAD_PARAMETERS;

    size_t cipher_len = params[0].memref.size - HESIA_HDR_LEN;
    if (params[1].memref.size < cipher_len) {
        params[1].memref.size = cipher_len;
        return TEE_ERROR_SHORT_BUFFER;
    }

    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_Result res = get_or_create_aes_key(&key);
    if (res != TEE_SUCCESS)
        return res;

    TEE_OperationHandle op = TEE_HANDLE_NULL;
    res = TEE_AllocateOperation(&op, TEE_ALG_AES_GCM, TEE_MODE_DECRYPT, 256);
    if (res != TEE_SUCCESS) {
        TEE_CloseObject(key);
        return res;
    }

    res = TEE_SetOperationKey(op, key);
    if (res != TEE_SUCCESS)
        goto out;

    const uint8_t *iv = in + 5;
    const uint8_t *tag = in + 5 + HESIA_IV_LEN;
    const uint8_t *cipher = in + HESIA_HDR_LEN;

    res = TEE_AEInit(op, iv, HESIA_IV_LEN, HESIA_TAG_LEN * 8, aad_len, 0);
    if (res != TEE_SUCCESS)
        goto out;

    res = update_aad_if_present(op, aad, aad_len);
    if (res != TEE_SUCCESS)
        goto out;

    size_t out_len = params[1].memref.size;
    res = TEE_AEDecryptFinal(op,
                             cipher, cipher_len,
                             params[1].memref.buffer, &out_len,
                             (void *)tag, HESIA_TAG_LEN);
    if (res != TEE_SUCCESS)
        goto out;

    params[1].memref.size = out_len;
    res = TEE_SUCCESS;

out:
    TEE_FreeOperation(op);
    TEE_CloseObject(key);
    return res;
}

TEE_Result TA_CreateEntryPoint(void)
{
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[4],
                                    void **sess_ctx)
{
    const uint32_t no_auth = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
                                             TEE_PARAM_TYPE_NONE,
                                             TEE_PARAM_TYPE_NONE,
                                             TEE_PARAM_TYPE_NONE);
    if (param_types == no_auth) {
        *sess_ctx = HESIA_SESS_CTX_RECOVERY;
        return TEE_SUCCESS;
    }

    TEE_Result res = verify_session_auth_or_bootstrap(param_types, params);
    if (res != TEE_SUCCESS)
        return res;

    *sess_ctx = HESIA_SESS_CTX_AUTH;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx)
{
    (void)sess_ctx;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types, TEE_Param params[4])
{
    if (sess_ctx != HESIA_SESS_CTX_AUTH) {
        switch (cmd_id) {
        case TA_HESIA_CMD_GET_RECOVERY_CHALLENGE:
            return get_recovery_challenge_cmd(param_types, params);
        case TA_HESIA_CMD_RECOVER_SESSION_AUTH:
            return recover_session_auth_cmd(param_types, params);
        default:
            return TEE_ERROR_ACCESS_DENIED;
        }
    }

    switch (cmd_id) {
    case TA_HESIA_CMD_SEAL:
        return seal_secret(param_types, params);
    case TA_HESIA_CMD_UNSEAL:
        return unseal_secret(param_types, params);
#ifdef HESIA_TA_ENABLE_MAINTENANCE_CMDS
    case TA_HESIA_CMD_ROTATE_KEY:
        return rotate_key_cmd(param_types);
    case TA_HESIA_CMD_WIPE_KEY:
        return wipe_key_cmd(param_types);
    case TA_HESIA_CMD_RESET_VERSION:
        return reset_version_cmd(param_types, params);
#endif
    case TA_HESIA_CMD_HKDF:
        return hkdf_cmd(param_types, params);
    case TA_HESIA_CMD_CHECK_VERSION:
        return check_version_cmd(param_types, params);
    case TA_HESIA_CMD_READ_VERSION:
        return read_version_cmd(param_types, params);
    case TA_HESIA_CMD_EXPORT_ATTEST_PUBKEY:
        return export_attest_pubkey_cmd(param_types, params);
    case TA_HESIA_CMD_SIGN_ATTEST_DIGEST:
        return sign_attest_digest_cmd(param_types, params);
    case TA_HESIA_CMD_SET_SESSION_AUTH_SECRET:
        return set_session_auth_secret_cmd(param_types, params);
    case TA_HESIA_CMD_STAGE_SLOT_UPDATE:
        return stage_slot_update_cmd(param_types, params);
    case TA_HESIA_CMD_COMMIT_SLOT_BOOT:
        return commit_slot_boot_cmd(param_types, params);
    case TA_HESIA_CMD_READ_SLOT_META:
        return read_slot_meta_cmd(param_types, params);
    case TA_HESIA_CMD_GET_RECOVERY_CHALLENGE:
        return get_recovery_challenge_cmd(param_types, params);
    case TA_HESIA_CMD_RECOVER_SESSION_AUTH:
        return recover_session_auth_cmd(param_types, params);
    case TA_HESIA_CMD_IMPORT_MLDSA_KEY_BLOB:
        return import_mldsa_key_blob_cmd(param_types, params);
    case TA_HESIA_CMD_EXPORT_MLDSA_PUBKEY:
        return export_mldsa_pubkey_cmd(param_types, params);
    case TA_HESIA_CMD_SIGN_MLDSA_PAYLOAD:
        return sign_mldsa_payload_cmd(param_types, params);
    case TA_HESIA_CMD_GET_MLDSA_STATUS:
        return get_mldsa_status_cmd(param_types, params);
    case TA_HESIA_CMD_GENERATE_MLDSA_KEYPAIR:
        return generate_mldsa_keypair_cmd(param_types, params);
    default:
        return TEE_ERROR_NOT_SUPPORTED;
    }
}
