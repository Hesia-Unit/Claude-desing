#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tee_client_api.h>
#include <ta_hesia.h>

#define HESIA_HDR_LEN (4 + 1 + 12 + 16)
#define HESIA_SESSION_AUTH_LEN 32
#define HESIA_SLOT_REQ_MAGIC 0x48535531u
#define HESIA_SLOT_META_MAGIC 0x48534D31u

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

static uint8_t parse_slot_id(const char *value)
{
    if (strcmp(value, "A") == 0 || strcmp(value, "a") == 0 ||
        strcmp(value, "slot_a") == 0 || strcmp(value, "slot-a") == 0) {
        return 1;
    }
    if (strcmp(value, "B") == 0 || strcmp(value, "b") == 0 ||
        strcmp(value, "slot_b") == 0 || strcmp(value, "slot-b") == 0) {
        return 2;
    }
    return 0;
}

static uint32_t parse_mldsa_slot_id(const char *value)
{
    if (value == NULL || *value == '\0' ||
        strcmp(value, "drone") == 0 || strcmp(value, "default") == 0) {
        return HESIA_MLDSA_SLOT_DRONE;
    }
    if (strcmp(value, "server") == 0) {
        return HESIA_MLDSA_SLOT_SERVER;
    }
    return UINT32_MAX;
}

static uint32_t current_mldsa_slot(void)
{
    const char *env = getenv("HESIA_OPTEE_MLDSA_SLOT");
    const uint32_t slot = parse_mldsa_slot_id(env);
    if (slot == UINT32_MAX) {
        fprintf(stderr, "Unsupported HESIA_OPTEE_MLDSA_SLOT value: %s\n", env ? env : "(null)");
        exit(2);
    }
    return slot;
}

static const char *mldsa_import_stage_label(uint32_t stage)
{
    switch (stage) {
    case HESIA_MLDSA_IMPORT_STAGE_INIT:
        return "init";
    case HESIA_MLDSA_IMPORT_STAGE_ALLOC:
        return "alloc";
    case HESIA_MLDSA_IMPORT_STAGE_UNSEAL:
        return "unseal";
    case HESIA_MLDSA_IMPORT_STAGE_PARSE:
        return "parse";
    case HESIA_MLDSA_IMPORT_STAGE_BACKEND_READY:
        return "backend_ready";
    case HESIA_MLDSA_IMPORT_STAGE_SELFTEST_SIGN:
        return "selftest_sign";
    case HESIA_MLDSA_IMPORT_STAGE_SELFTEST_VERIFY:
        return "selftest_verify";
    case HESIA_MLDSA_IMPORT_STAGE_PERSIST:
        return "persist";
    case HESIA_MLDSA_IMPORT_STAGE_DONE:
        return "done";
    default:
        return "unknown";
    }
}

static const char *mldsa_keygen_stage_label(uint32_t stage)
{
    switch (stage) {
    case HESIA_MLDSA_KEYGEN_STAGE_INIT:
        return "init";
    case HESIA_MLDSA_KEYGEN_STAGE_ALLOC:
        return "alloc";
    case HESIA_MLDSA_KEYGEN_STAGE_GENERATE:
        return "generate";
    case HESIA_MLDSA_KEYGEN_STAGE_SERIALIZE:
        return "serialize";
    case HESIA_MLDSA_KEYGEN_STAGE_PERSIST:
        return "persist";
    case HESIA_MLDSA_KEYGEN_STAGE_DONE:
        return "done";
    default:
        return "unknown";
    }
}

static int extract_attest_pubkey_from_recovery_challenge(const unsigned char *challenge_buf,
                                                         size_t challenge_len,
                                                         unsigned char *pubkey_out,
                                                         size_t pubkey_out_len)
{
    const hesia_recovery_challenge_t *challenge = NULL;

    if (!challenge_buf || !pubkey_out)
        return -1;
    if (challenge_len < sizeof(hesia_recovery_challenge_t))
        return -1;
    if (pubkey_out_len < HESIA_RECOVERY_ATTEST_PUBKEY_LEN)
        return -1;

    challenge = (const hesia_recovery_challenge_t *)challenge_buf;
    if (challenge->magic != HESIA_RECOVERY_MAGIC ||
        challenge->version != HESIA_RECOVERY_VERSION) {
        return -1;
    }

    memcpy(pubkey_out, challenge->attest_pubkey,
           HESIA_RECOVERY_ATTEST_PUBKEY_LEN);
    return (int)HESIA_RECOVERY_ATTEST_PUBKEY_LEN;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s seal <in> <out>\n", prog);
    fprintf(stderr, "  %s unseal <in> <out>\n", prog);
    fprintf(stderr, "  %s rotate         (maintenance TA builds only)\n", prog);
    fprintf(stderr, "  %s wipe           (maintenance TA builds only)\n", prog);
    fprintf(stderr, "  %s reset_version  (maintenance TA builds only)\n", prog);
    fprintf(stderr, "  %s read_version\n", prog);
    fprintf(stderr, "  %s stage_slot_update <slot> <firmware_version> <asset_version>\n", prog);
    fprintf(stderr, "  %s commit_slot_boot <slot> <firmware_version> <asset_version>\n", prog);
    fprintf(stderr, "  %s read_slot_meta\n", prog);
    fprintf(stderr, "  %s rotate_session_auth <new_secret_file> [sealed_out]\n", prog);
    fprintf(stderr, "  %s recovery_challenge <challenge_out>\n", prog);
    fprintf(stderr, "  %s recover_session_auth <token_file> <new_secret_file> [sealed_out]\n", prog);
    fprintf(stderr, "  %s generate_mldsa_keypair <public_out>\n", prog);
    fprintf(stderr, "  %s import_mldsa_key_blob <sealed_blob>\n", prog);
    fprintf(stderr, "  %s export_attest_pubkey <public_out>\n", prog);
    fprintf(stderr, "  %s export_mldsa_pubkey <public_out>\n", prog);
    fprintf(stderr, "  %s sign_mldsa_payload <payload_in> <signature_out>\n", prog);
    fprintf(stderr, "  %s mldsa_status\n", prog);
    fprintf(stderr, "\nEnvironment:\n");
    fprintf(stderr, "  HESIA_OPTEE_SESSION_AUTH_PATH   Current session-auth sealed blob ");
    fprintf(stderr, "(default: /etc/hesia/secure/optee_session_auth.sealed)\n");
    fprintf(stderr, "  HESIA_OPTEE_MLDSA_SLOT         ML-DSA key slot: drone (default) or server\n");
}

static int read_file(const char *path, unsigned char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    unsigned char *tmp = (unsigned char *)malloc((size_t)sz);
    if (!tmp) {
        fclose(f);
        return -1;
    }
    if (sz > 0 && fread(tmp, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(tmp);
        return -1;
    }
    fclose(f);
    *buf = tmp;
    *len = (size_t)sz;
    return 0;
}

static int write_file(const char *path, const unsigned char *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (len > 0 && fwrite(buf, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void wipe_and_free(unsigned char **buf, size_t len)
{
    if (buf && *buf) {
        volatile unsigned char *p = *buf;
        size_t i;
        for (i = 0; i < len; ++i)
            p[i] = 0;
        free(*buf);
        *buf = NULL;
    }
}

static const char *current_session_auth_path(void)
{
    const char *env = getenv("HESIA_OPTEE_SESSION_AUTH_PATH");
    if (env && env[0] != '\0')
        return env;
    return "/etc/hesia/secure/optee_session_auth.sealed";
}

static const char *aad_label_for_path(const char *path)
{
    const char *slash = NULL;
    const char *backslash = NULL;
    const char *base = path;

    if (!path || !*path)
        return NULL;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash && slash + 1 > base)
        base = slash + 1;
    if (backslash && backslash + 1 > base)
        base = backslash + 1;
    if (strcmp(base, "optee_session_auth.sealed") == 0)
        return "HESIA|TA|session_auth|v1";
    if (strcmp(base, "dilithium5_sk.sealed") == 0)
        return "HESIA|TA|mldsa_drone|v1";
    if (strcmp(base, "server_secret.bin.sealed") == 0)
        return "HESIA|TA|mldsa_server|v1";
    return base;
}

static int is_legacy_sealed_blob_name(const char *path)
{
    const char *label = aad_label_for_path(path);
    if (!label)
        return 0;
    return strcmp(label, "hesia_seed.sealed") == 0 ||
           strcmp(label, "dilithium5_sk.sealed") == 0 ||
           strcmp(label, "server_secret.bin.sealed") == 0;
}

static int is_legacy_unseal_retryable_result(TEEC_Result res)
{
    return res == TEEC_ERROR_BAD_PARAMETERS ||
           res == TEE_ERROR_MAC_INVALID;
}

static int load_session_auth_material(int required, unsigned char **buf, size_t *len)
{
    const char *path = current_session_auth_path();
    if (read_file(path, buf, len) != 0) {
        if (required) {
            fprintf(stderr, "Failed to read OP-TEE session auth material: %s\n", path);
            return -1;
        }
        *buf = NULL;
        *len = 0;
        return 0;
    }

    if (*len == 0) {
        fprintf(stderr, "OP-TEE session auth material is empty\n");
        free(*buf);
        *buf = NULL;
        *len = 0;
        return -1;
    }
    return 0;
}

static TEEC_Result open_session_with_secret(TEEC_Context *ctx,
                                            TEEC_Session *sess,
                                            TEEC_UUID *uuid,
                                            unsigned char *auth_material,
                                            size_t auth_material_len,
                                            uint32_t *err_origin)
{
    TEEC_Result res = TEEC_InitializeContext(NULL, ctx);
    TEEC_Operation open_op;
    TEEC_Operation *open_op_ptr = NULL;

    if (res != TEEC_SUCCESS)
        return res;

    memset(&open_op, 0, sizeof(open_op));
    if (auth_material && auth_material_len > 0) {
        open_op_ptr = &open_op;
        open_op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                              TEEC_NONE,
                                              TEEC_NONE,
                                              TEEC_NONE);
        open_op.params[0].tmpref.buffer = auth_material;
        open_op.params[0].tmpref.size = auth_material_len;
    }

    res = TEEC_OpenSession(ctx, sess, uuid, TEEC_LOGIN_PUBLIC, NULL, open_op_ptr, err_origin);
    if (res != TEEC_SUCCESS)
        TEEC_FinalizeContext(ctx);
    return res;
}

static int seal_session_auth_blob(TEEC_Session *sess,
                                  unsigned char *secret_buf,
                                  size_t secret_len,
                                  const char *sealed_path,
                                  uint32_t *err_origin)
{
    TEEC_Operation seal_op;
    unsigned char *sealed_buf = NULL;
    size_t sealed_len = secret_len + HESIA_HDR_LEN;
    const char *aad_label = aad_label_for_path(sealed_path);
    TEEC_Result res;

    if (!sealed_path || sealed_path[0] == '\0') {
        fprintf(stderr, "Missing sealed session auth output path\n");
        return -1;
    }

    sealed_buf = (unsigned char *)malloc(sealed_len);
    if (!sealed_buf) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    memset(&seal_op, 0, sizeof(seal_op));
    seal_op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                          TEEC_MEMREF_TEMP_OUTPUT,
                                          aad_label ? TEEC_MEMREF_TEMP_INPUT : TEEC_NONE,
                                          TEEC_NONE);
    seal_op.params[0].tmpref.buffer = secret_buf;
    seal_op.params[0].tmpref.size = secret_len;
    seal_op.params[1].tmpref.buffer = sealed_buf;
    seal_op.params[1].tmpref.size = sealed_len;
    if (aad_label) {
        seal_op.params[2].tmpref.buffer = (void *)aad_label;
        seal_op.params[2].tmpref.size = strlen(aad_label);
    }

    res = TEEC_InvokeCommand(sess, TA_HESIA_CMD_SEAL, &seal_op, err_origin);
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_InvokeCommand(seal session auth) failed: 0x%x origin 0x%x\n",
                res, *err_origin);
        wipe_and_free(&sealed_buf, sealed_len);
        return -1;
    }

    sealed_len = seal_op.params[1].tmpref.size;
    if (write_file(sealed_path, sealed_buf, sealed_len) != 0) {
        fprintf(stderr, "Failed to write sealed session auth blob: %s\n", sealed_path);
        wipe_and_free(&sealed_buf, sealed_len);
        return -1;
    }

    wipe_and_free(&sealed_buf, sealed_len);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 5) {
        usage(argv[0]);
        return 1;
    }

    int do_seal = 0;
    int do_unseal = 0;
    int do_rotate = 0;
    int do_wipe = 0;
    int do_reset_version = 0;
    int do_read_version = 0;
    int do_stage_slot_update = 0;
    int do_commit_slot_boot = 0;
    int do_read_slot_meta = 0;
    int do_rotate_session_auth = 0;
    int do_recovery_challenge = 0;
    int do_recover_session_auth = 0;
    int do_generate_mldsa_keypair = 0;
    int do_import_mldsa_key_blob = 0;
    int do_export_attest_pubkey = 0;
    int do_export_mldsa_pubkey = 0;
    int do_sign_mldsa_payload = 0;
    int do_get_mldsa_status = 0;

    if (strcmp(argv[1], "seal") == 0) {
        do_seal = 1;
    } else if (strcmp(argv[1], "unseal") == 0) {
        do_unseal = 1;
    } else if (strcmp(argv[1], "rotate") == 0) {
        do_rotate = 1;
    } else if (strcmp(argv[1], "wipe") == 0) {
        do_wipe = 1;
    } else if (strcmp(argv[1], "reset_version") == 0) {
        do_reset_version = 1;
    } else if (strcmp(argv[1], "read_version") == 0) {
        do_read_version = 1;
    } else if (strcmp(argv[1], "stage_slot_update") == 0) {
        do_stage_slot_update = 1;
    } else if (strcmp(argv[1], "commit_slot_boot") == 0) {
        do_commit_slot_boot = 1;
    } else if (strcmp(argv[1], "read_slot_meta") == 0) {
        do_read_slot_meta = 1;
    } else if (strcmp(argv[1], "provision_session_auth") == 0 ||
               strcmp(argv[1], "restore_session_auth_blob") == 0) {
        fprintf(stderr, "Legacy insecure bootstrap commands were removed; use recovery_challenge + recover_session_auth\n");
        return 2;
    } else if (strcmp(argv[1], "rotate_session_auth") == 0) {
        do_rotate_session_auth = 1;
    } else if (strcmp(argv[1], "recovery_challenge") == 0) {
        do_recovery_challenge = 1;
    } else if (strcmp(argv[1], "recover_session_auth") == 0) {
        do_recover_session_auth = 1;
    } else if (strcmp(argv[1], "generate_mldsa_keypair") == 0) {
        do_generate_mldsa_keypair = 1;
    } else if (strcmp(argv[1], "import_mldsa_key_blob") == 0) {
        do_import_mldsa_key_blob = 1;
    } else if (strcmp(argv[1], "export_attest_pubkey") == 0) {
        do_export_attest_pubkey = 1;
    } else if (strcmp(argv[1], "export_mldsa_pubkey") == 0) {
        do_export_mldsa_pubkey = 1;
    } else if (strcmp(argv[1], "sign_mldsa_payload") == 0) {
        do_sign_mldsa_payload = 1;
    } else if (strcmp(argv[1], "mldsa_status") == 0) {
        do_get_mldsa_status = 1;
    } else {
        usage(argv[0]);
        return 1;
    }

    if ((do_seal || do_unseal) && argc != 4) {
        usage(argv[0]);
        return 1;
    }
    if (do_rotate_session_auth && !(argc == 3 || argc == 4)) {
        usage(argv[0]);
        return 1;
    }
    if ((do_stage_slot_update || do_commit_slot_boot) && argc != 5) {
        usage(argv[0]);
        return 1;
    }
    if (do_read_slot_meta && argc != 2) {
        usage(argv[0]);
        return 1;
    }
    if (do_recovery_challenge && argc != 3) {
        usage(argv[0]);
        return 1;
    }
    if (do_recover_session_auth && !(argc == 4 || argc == 5)) {
        usage(argv[0]);
        return 1;
    }
    if (do_generate_mldsa_keypair && argc != 3) {
        usage(argv[0]);
        return 1;
    }
    if ((do_import_mldsa_key_blob || do_export_attest_pubkey || do_export_mldsa_pubkey) &&
        argc != 3) {
        usage(argv[0]);
        return 1;
    }
    if (do_sign_mldsa_payload && argc != 4) {
        usage(argv[0]);
        return 1;
    }
    if (do_get_mldsa_status && argc != 2) {
        usage(argv[0]);
        return 1;
    }

    unsigned char *in_buf = NULL;
    size_t in_len = 0;
    size_t out_len = 0;
    unsigned char *out_buf = NULL;
    unsigned char *auth_buf = NULL;
    size_t auth_len = 0;
    unsigned char *new_auth_buf = NULL;
    size_t new_auth_len = 0;
    const char *sealed_out_path = NULL;
    const char *seal_aad_label = NULL;
    int session_requires_reopen = 0;
    hesia_slot_request_t slot_req;
    hesia_slot_meta_t slot_meta;
    uint8_t slot_id = 0;
    uint32_t mldsa_slot = HESIA_MLDSA_SLOT_DRONE;

    memset(&slot_req, 0, sizeof(slot_req));
    memset(&slot_meta, 0, sizeof(slot_meta));

    if (do_generate_mldsa_keypair || do_import_mldsa_key_blob ||
        do_export_mldsa_pubkey || do_sign_mldsa_payload || do_get_mldsa_status) {
        mldsa_slot = current_mldsa_slot();
    }

    if (do_rotate_session_auth) {
        sealed_out_path = (argc == 4) ? argv[3] : current_session_auth_path();
    } else if (do_recover_session_auth) {
        sealed_out_path = (argc == 5) ? argv[4] : current_session_auth_path();
    }

    if (do_seal || do_unseal) {
        seal_aad_label = aad_label_for_path(do_seal ? argv[3] : argv[2]);
        if (read_file(argv[2], &in_buf, &in_len) != 0) {
            fprintf(stderr, "Failed to read input file\n");
            return 1;
        }

        out_len = do_seal ? (in_len + HESIA_HDR_LEN) : in_len;
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            free(in_buf);
            return 1;
        }
    }

    if (do_recovery_challenge) {
        out_len = sizeof(hesia_recovery_challenge_t);
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
    }
    if (do_generate_mldsa_keypair) {
        out_len = 2592;
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
    }
    if (do_export_attest_pubkey) {
        out_len = HESIA_RECOVERY_ATTEST_PUBKEY_LEN;
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
    }
    if (do_export_mldsa_pubkey) {
        out_len = 2592;
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
    }
    if (do_sign_mldsa_payload) {
        if (read_file(argv[2], &in_buf, &in_len) != 0 || in_len == 0) {
            fprintf(stderr, "Failed to read ML-DSA payload input\n");
            free(out_buf);
            return 1;
        }
        out_len = 4627;
        out_buf = (unsigned char *)malloc(out_len);
        if (!out_buf) {
            fprintf(stderr, "Out of memory\n");
            free(in_buf);
            return 1;
        }
    }

    if (do_recover_session_auth) {
        if (read_file(argv[2], &in_buf, &in_len) != 0 ||
            in_len != sizeof(hesia_recovery_token_t)) {
            fprintf(stderr, "Failed to read recovery token file\n");
            free(out_buf);
            return 1;
        }
    }

    if (do_rotate_session_auth) {
        if (read_file(argv[2], &new_auth_buf, &new_auth_len) != 0 ||
            new_auth_len != HESIA_SESSION_AUTH_LEN) {
            fprintf(stderr, "Failed to read 32-byte session auth secret file\n");
            free(in_buf);
            free(out_buf);
            return 1;
        }
    }
    if (do_import_mldsa_key_blob) {
        if (read_file(argv[2], &in_buf, &in_len) != 0 || in_len == 0) {
            fprintf(stderr, "Failed to read sealed ML-DSA blob\n");
            free(out_buf);
            return 1;
        }
    }
    if (do_recover_session_auth) {
        if (read_file(argv[3], &new_auth_buf, &new_auth_len) != 0 ||
            new_auth_len != HESIA_SESSION_AUTH_LEN) {
            fprintf(stderr, "Failed to read 32-byte session auth secret file\n");
            free(in_buf);
            free(out_buf);
            return 1;
        }
        session_requires_reopen = 1;
    }

    if (!do_recovery_challenge && !do_recover_session_auth) {
        if (load_session_auth_material(do_rotate_session_auth || do_seal || do_unseal ||
                                       do_rotate || do_wipe || do_reset_version || do_read_version ||
                                       do_stage_slot_update || do_commit_slot_boot || do_read_slot_meta ||
                                       do_generate_mldsa_keypair || do_import_mldsa_key_blob ||
                                       do_export_attest_pubkey ||
                                       do_export_mldsa_pubkey || do_sign_mldsa_payload ||
                                       do_get_mldsa_status,
                                       &auth_buf, &auth_len) != 0) {
            free(in_buf);
            free(out_buf);
            wipe_and_free(&new_auth_buf, new_auth_len);
            return 1;
        }
    }

    if (do_stage_slot_update || do_commit_slot_boot) {
        char *endptr = NULL;
        slot_id = parse_slot_id(argv[2]);
        if (slot_id == 0) {
            fprintf(stderr, "Invalid slot id: %s\n", argv[2]);
            goto out;
        }
        slot_req.magic = HESIA_SLOT_REQ_MAGIC;
        slot_req.slot = slot_id;
        slot_req.firmware_version = strtoull(argv[3], &endptr, 10);
        if (!endptr || *endptr != '\0' || slot_req.firmware_version == 0) {
            fprintf(stderr, "Invalid firmware version: %s\n", argv[3]);
            goto out;
        }
        slot_req.asset_version = strtoull(argv[4], &endptr, 10);
        if (!endptr || *endptr != '\0' || slot_req.asset_version == 0) {
            fprintf(stderr, "Invalid asset version: %s\n", argv[4]);
            goto out;
        }
    }

    TEEC_Result res;
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_UUID uuid = TA_HESIA_UUID;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    {
        unsigned char *session_auth_material = NULL;
        size_t session_auth_material_len = 0;
        if (!(do_recovery_challenge || do_recover_session_auth)) {
            session_auth_material = auth_buf;
            session_auth_material_len = auth_len;
        }

        res = open_session_with_secret(&ctx, &sess, &uuid,
                                       session_auth_material,
                                       session_auth_material_len,
                                       &err_origin);
    }
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_OpenSession failed: 0x%x origin 0x%x\n",
                res, err_origin);
        goto out;
    }

    memset(&op, 0, sizeof(op));
    uint32_t cmd = 0;
    if (do_seal || do_unseal) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT,
                                         seal_aad_label ? TEEC_MEMREF_TEMP_INPUT : TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[1].tmpref.buffer = out_buf;
        op.params[1].tmpref.size = out_len;
        if (seal_aad_label) {
            op.params[2].tmpref.buffer = (void *)seal_aad_label;
            op.params[2].tmpref.size = strlen(seal_aad_label);
        }
        cmd = do_seal ? TA_HESIA_CMD_SEAL : TA_HESIA_CMD_UNSEAL;
    } else if (do_recovery_challenge) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_NONE,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = out_buf;
        op.params[0].tmpref.size = out_len;
        cmd = TA_HESIA_CMD_GET_RECOVERY_CHALLENGE;
    } else if (do_recover_session_auth) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[1].tmpref.buffer = new_auth_buf;
        op.params[1].tmpref.size = new_auth_len;
        cmd = TA_HESIA_CMD_RECOVER_SESSION_AUTH;
    } else if (do_rotate_session_auth) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_NONE,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = new_auth_buf;
        op.params[0].tmpref.size = new_auth_len;
        cmd = TA_HESIA_CMD_SET_SESSION_AUTH_SECRET;
    } else if (do_stage_slot_update || do_commit_slot_boot) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_NONE,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = &slot_req;
        op.params[0].tmpref.size = sizeof(slot_req);
        cmd = do_stage_slot_update ? TA_HESIA_CMD_STAGE_SLOT_UPDATE : TA_HESIA_CMD_COMMIT_SLOT_BOOT;
    } else if (do_read_slot_meta) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_NONE,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = &slot_meta;
        op.params[0].tmpref.size = sizeof(slot_meta);
        cmd = TA_HESIA_CMD_READ_SLOT_META;
    } else if (do_generate_mldsa_keypair) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_VALUE_OUTPUT,
                                         TEEC_VALUE_INPUT,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = out_buf;
        op.params[0].tmpref.size = out_len;
        op.params[2].value.a = mldsa_slot;
        cmd = TA_HESIA_CMD_GENERATE_MLDSA_KEYPAIR;
    } else if (do_import_mldsa_key_blob) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_VALUE_OUTPUT,
                                         TEEC_VALUE_INPUT,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[2].value.a = mldsa_slot;
        cmd = TA_HESIA_CMD_IMPORT_MLDSA_KEY_BLOB;
    } else if (do_export_attest_pubkey) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_NONE,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = out_buf;
        op.params[0].tmpref.size = out_len;
        cmd = TA_HESIA_CMD_EXPORT_ATTEST_PUBKEY;
    } else if (do_export_mldsa_pubkey) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_VALUE_INPUT,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = out_buf;
        op.params[0].tmpref.size = out_len;
        op.params[1].value.a = mldsa_slot;
        cmd = TA_HESIA_CMD_EXPORT_MLDSA_PUBKEY;
    } else if (do_sign_mldsa_payload) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_VALUE_INPUT,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[1].tmpref.buffer = out_buf;
        op.params[1].tmpref.size = out_len;
        op.params[2].value.a = mldsa_slot;
        cmd = TA_HESIA_CMD_SIGN_MLDSA_PAYLOAD;
    } else if (do_get_mldsa_status) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
                                         TEEC_VALUE_INPUT,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[1].value.a = mldsa_slot;
        cmd = TA_HESIA_CMD_GET_MLDSA_STATUS;
    } else {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
        if (do_rotate) {
            cmd = TA_HESIA_CMD_ROTATE_KEY;
        } else if (do_wipe) {
            cmd = TA_HESIA_CMD_WIPE_KEY;
        } else if (do_reset_version) {
            cmd = TA_HESIA_CMD_RESET_VERSION;
        } else if (do_read_version) {
            op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
                                             TEEC_NONE,
                                             TEEC_NONE,
                                             TEEC_NONE);
            cmd = TA_HESIA_CMD_READ_VERSION;
        } else {
            fprintf(stderr, "Invalid command\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }

    res = TEEC_InvokeCommand(&sess, cmd, &op, &err_origin);
    if (res != TEEC_SUCCESS &&
        do_unseal &&
        seal_aad_label &&
        is_legacy_sealed_blob_name(argv[2]) &&
        is_legacy_unseal_retryable_result(res)) {
        TEEC_Result legacy_res;
        memset(&op, 0, sizeof(op));
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[1].tmpref.buffer = out_buf;
        op.params[1].tmpref.size = out_len;
        legacy_res = TEEC_InvokeCommand(&sess, cmd, &op, &err_origin);
        if (legacy_res == TEEC_ERROR_SHORT_BUFFER) {
            out_len = op.params[1].tmpref.size;
            free(out_buf);
            out_buf = (unsigned char *)malloc(out_len);
            if (!out_buf) {
                fprintf(stderr, "Out of memory\n");
                TEEC_CloseSession(&sess);
                TEEC_FinalizeContext(&ctx);
                goto out;
            }
            memset(&op, 0, sizeof(op));
            op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                             TEEC_MEMREF_TEMP_OUTPUT,
                                             TEEC_NONE,
                                             TEEC_NONE);
            op.params[0].tmpref.buffer = in_buf;
            op.params[0].tmpref.size = in_len;
            op.params[1].tmpref.buffer = out_buf;
            op.params[1].tmpref.size = out_len;
            legacy_res = TEEC_InvokeCommand(&sess, cmd, &op, &err_origin);
        }
        res = legacy_res;
    }
    if (res != TEEC_SUCCESS && do_export_attest_pubkey) {
        unsigned char challenge_buf[sizeof(hesia_recovery_challenge_t)];
        TEEC_Operation fallback_op;
        int copied = -1;

        memset(&fallback_op, 0, sizeof(fallback_op));
        memset(challenge_buf, 0, sizeof(challenge_buf));
        fallback_op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                                  TEEC_NONE,
                                                  TEEC_NONE,
                                                  TEEC_NONE);
        fallback_op.params[0].tmpref.buffer = challenge_buf;
        fallback_op.params[0].tmpref.size = sizeof(challenge_buf);
        res = TEEC_InvokeCommand(&sess, TA_HESIA_CMD_GET_RECOVERY_CHALLENGE,
                                 &fallback_op, &err_origin);
        if (res == TEEC_SUCCESS) {
            copied = extract_attest_pubkey_from_recovery_challenge(
                challenge_buf,
                fallback_op.params[0].tmpref.size,
                out_buf,
                out_len);
            if (copied == HESIA_RECOVERY_ATTEST_PUBKEY_LEN) {
                out_len = (size_t)copied;
            } else {
                fprintf(stderr, "Recovery challenge fallback did not contain a valid attestation public key\n");
                res = TEEC_ERROR_GENERIC;
            }
        }
    }
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_InvokeCommand failed: 0x%x origin 0x%x\n",
                res, err_origin);
        if (do_generate_mldsa_keypair) {
            fprintf(stderr, "ML-DSA keygen stage marker: %u (%s), ta_res=0x%x, size=%zu\n",
                    op.params[1].value.a,
                    mldsa_keygen_stage_label(op.params[1].value.a),
                    op.params[1].value.b,
                    op.params[0].tmpref.size);
        }
        if (do_import_mldsa_key_blob) {
            fprintf(stderr, "ML-DSA import stage marker: %u (%s), ta_res=0x%x\n",
                    op.params[1].value.a,
                    mldsa_import_stage_label(op.params[1].value.a),
                    op.params[1].value.b);
        }
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        goto out;
    }

    if (do_generate_mldsa_keypair && op.params[1].value.b != TEEC_SUCCESS) {
        fprintf(stderr, "ML-DSA keygen failed inside TA: stage=%u (%s), ta_res=0x%x, size=%zu\n",
                op.params[1].value.a,
                mldsa_keygen_stage_label(op.params[1].value.a),
                op.params[1].value.b,
                op.params[0].tmpref.size);
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        goto out;
    }
    if (do_import_mldsa_key_blob && op.params[1].value.b != TEEC_SUCCESS) {
        fprintf(stderr, "ML-DSA import failed inside TA: stage=%u (%s), ta_res=0x%x\n",
                op.params[1].value.a,
                mldsa_import_stage_label(op.params[1].value.a),
                op.params[1].value.b);
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        goto out;
    }

    if (do_seal || do_unseal) {
        out_len = op.params[1].tmpref.size;
        if (write_file(argv[3], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write output file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }

    if (do_recovery_challenge) {
        out_len = op.params[0].tmpref.size;
        if (write_file(argv[2], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write recovery challenge file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }
    if (do_generate_mldsa_keypair) {
        out_len = op.params[0].tmpref.size;
        if (write_file(argv[2], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write ML-DSA public key file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }
    if (do_export_mldsa_pubkey) {
        out_len = op.params[0].tmpref.size;
        if (write_file(argv[2], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write exported ML-DSA public key file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }
    if (do_export_attest_pubkey) {
        if (write_file(argv[2], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write exported attestation public key file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }
    if (do_sign_mldsa_payload) {
        out_len = op.params[1].tmpref.size;
        if (write_file(argv[3], out_buf, out_len) != 0) {
            fprintf(stderr, "Failed to write ML-DSA signature file\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }

    if (session_requires_reopen) {
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        res = open_session_with_secret(&ctx, &sess, &uuid,
                                       new_auth_buf, new_auth_len,
                                       &err_origin);
        if (res != TEEC_SUCCESS) {
            fprintf(stderr, "TEEC_OpenSession(recovery reopen) failed: 0x%x origin 0x%x\n",
                    res, err_origin);
            goto out;
        }
    }

    if (do_rotate_session_auth || do_recover_session_auth) {
        if (seal_session_auth_blob(&sess, new_auth_buf, new_auth_len, sealed_out_path, &err_origin) != 0) {
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
    }

    if (do_read_version) {
        uint64_t v = ((uint64_t)op.params[0].value.a << 32) | op.params[0].value.b;
        printf("%llu\n", (unsigned long long)v);
    }

    if (do_read_slot_meta) {
        if (slot_meta.magic != HESIA_SLOT_META_MAGIC) {
            fprintf(stderr, "Invalid slot metadata magic\n");
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            goto out;
        }
        printf("initialized=%u\n", slot_meta.initialized);
        printf("active_slot=%u\n", slot_meta.active_slot);
        printf("pending_slot=%u\n", slot_meta.pending_slot);
        printf("max_firmware_version=%llu\n", (unsigned long long)slot_meta.max_firmware_version);
        printf("slot_a_firmware_version=%llu\n", (unsigned long long)slot_meta.slot_a_firmware_version);
        printf("slot_b_firmware_version=%llu\n", (unsigned long long)slot_meta.slot_b_firmware_version);
        printf("slot_a_asset_version=%llu\n", (unsigned long long)slot_meta.slot_a_asset_version);
        printf("slot_b_asset_version=%llu\n", (unsigned long long)slot_meta.slot_b_asset_version);
        printf("pending_firmware_version=%llu\n", (unsigned long long)slot_meta.pending_firmware_version);
        printf("pending_asset_version=%llu\n", (unsigned long long)slot_meta.pending_asset_version);
    }
    if (do_get_mldsa_status) {
        printf("backend_ready=%u\n", op.params[0].value.a);
        printf("key_present=%u\n", op.params[0].value.b);
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    free(in_buf);
    free(out_buf);
    wipe_and_free(&auth_buf, auth_len);
    wipe_and_free(&new_auth_buf, new_auth_len);
    return 0;

out:
    free(in_buf);
    free(out_buf);
    wipe_and_free(&auth_buf, auth_len);
    wipe_and_free(&new_auth_buf, new_auth_len);
    return 1;
}
