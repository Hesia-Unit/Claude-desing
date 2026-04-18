/*
 * OP-TEE TA - HESIA
 * Seals/unseals sensitive data using an internal AES-256-GCM key
 * stored as a persistent object in TEE secure storage.
 */

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <ta_hesia.h>

#define HESIA_KEY_ID        "hesia_master_key_v1"
#define HESIA_KEY_ID_LEN    20

#define HESIA_FW_VERSION_ID     "hesia_fw_version_v4"
#define HESIA_FW_VERSION_ID_LEN (sizeof(HESIA_FW_VERSION_ID) - 1)

#define HESIA_MAGIC_0 'H'
#define HESIA_MAGIC_1 'E'
#define HESIA_MAGIC_2 'S'
#define HESIA_MAGIC_3 '1'
#define HESIA_VERSION 1

#define HESIA_IV_LEN  12
#define HESIA_TAG_LEN 16
#define HESIA_HDR_LEN (4 + 1 + HESIA_IV_LEN + HESIA_TAG_LEN)

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

static TEE_Result wipe_key_internal(void)
{
    TEE_ObjectHandle obj = TEE_HANDLE_NULL;
    uint32_t flags = TEE_DATA_FLAG_ACCESS_READ |
                     TEE_DATA_FLAG_ACCESS_WRITE |
                     TEE_DATA_FLAG_ACCESS_WRITE_META;
    TEE_Result res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE,
                                              HESIA_KEY_ID,
                                              HESIA_KEY_ID_LEN,
                                              flags, &obj);
    if (res == TEE_ERROR_ITEM_NOT_FOUND)
        return TEE_SUCCESS;
    if (res != TEE_SUCCESS)
        return res;

    TEE_CloseAndDeletePersistentObject1(obj);
    return TEE_SUCCESS;
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

#ifndef TEE_ALG_HMAC_SHA3_512
#error "OP-TEE missing HMAC-SHA3-512 support required for HKDF"
#endif

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

static TEE_Result hmac_sha3_512(const uint8_t* key, size_t key_len,
                                const uint8_t* msg, size_t msg_len,
                                uint8_t* out, size_t* out_len)
{
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    TEE_ObjectHandle hkey = TEE_HANDLE_NULL;
    TEE_Attribute attr;
    TEE_Result res;

    if (*out_len < 64)
        return TEE_ERROR_SHORT_BUFFER;

    res = TEE_AllocateOperation(&op, TEE_ALG_HMAC_SHA3_512, TEE_MODE_MAC, (uint32_t)(key_len * 8));
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_AllocateTransientObject(TEE_TYPE_HMAC_SHA3_512, (uint32_t)(key_len * 8), &hkey);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(op);
        return res;
    }

    TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, key, key_len);
    res = TEE_PopulateTransientObject(hkey, &attr, 1);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(op);
        TEE_FreeTransientObject(hkey);
        return res;
    }

    res = TEE_SetOperationKey(op, hkey);
    if (res != TEE_SUCCESS) {
        TEE_FreeOperation(op);
        TEE_FreeTransientObject(hkey);
        return res;
    }

    TEE_MACInit(op, NULL, 0);
    TEE_MACUpdate(op, msg, msg_len);
    res = TEE_MACComputeFinal(op, NULL, 0, out, out_len);

    TEE_FreeOperation(op);
    TEE_FreeTransientObject(hkey);
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
static TEE_Result seal_secret(uint32_t param_types, TEE_Param params[4])
{
    // params[0]: in  plaintext
    // params[1]: out sealed blob
    const uint32_t exp = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                         TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                         TEE_PARAM_TYPE_NONE,
                                         TEE_PARAM_TYPE_NONE);
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

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

    res = TEE_AEInit(op, iv, HESIA_IV_LEN, HESIA_TAG_LEN * 8, 0, 0);
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
    if (param_types != exp)
        return TEE_ERROR_BAD_PARAMETERS;

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

    res = TEE_AEInit(op, iv, HESIA_IV_LEN, HESIA_TAG_LEN * 8, 0, 0);
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
    (void)param_types;
    (void)params;
    (void)sess_ctx;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx)
{
    (void)sess_ctx;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types, TEE_Param params[4])
{
    (void)sess_ctx;
    switch (cmd_id) {
    case TA_HESIA_CMD_SEAL:
        return seal_secret(param_types, params);
    case TA_HESIA_CMD_UNSEAL:
        return unseal_secret(param_types, params);
    case TA_HESIA_CMD_ROTATE_KEY:
        return rotate_key_cmd(param_types);
    case TA_HESIA_CMD_WIPE_KEY:
        return wipe_key_cmd(param_types);
    case TA_HESIA_CMD_HKDF:
        return hkdf_cmd(param_types, params);
    case TA_HESIA_CMD_CHECK_VERSION:
        return check_version_cmd(param_types, params);
    case TA_HESIA_CMD_RESET_VERSION:
        return reset_version_cmd(param_types, params);
    case TA_HESIA_CMD_READ_VERSION:
        return read_version_cmd(param_types, params);
    default:
        return TEE_ERROR_NOT_SUPPORTED;
    }
}
