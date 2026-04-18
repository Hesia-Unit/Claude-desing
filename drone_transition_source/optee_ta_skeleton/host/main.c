#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tee_client_api.h>
#include <ta_hesia.h>

#define HESIA_HDR_LEN (4 + 1 + 12 + 16)

static void usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s seal <in> <out>\n", prog);
    fprintf(stderr, "  %s unseal <in> <out>\n", prog);
    fprintf(stderr, "  %s rotate\n", prog);
    fprintf(stderr, "  %s wipe\n", prog);
    fprintf(stderr, "  %s reset_version\n", prog);
    fprintf(stderr, "  %s read_version\n", prog);
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

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 4) {
        usage(argv[0]);
        return 1;
    }

    int do_seal = 0;
    int do_unseal = 0;
    int do_rotate = 0;
    int do_wipe = 0;
    int do_reset_version = 0;
    int do_read_version = 0;

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
    } else {
        usage(argv[0]);
        return 1;
    }

    if ((do_seal || do_unseal) && argc != 4) {
        usage(argv[0]);
        return 1;
    }

    unsigned char *in_buf = NULL;
    size_t in_len = 0;
    size_t out_len = 0;
    unsigned char *out_buf = NULL;

    if (do_seal || do_unseal) {
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

    TEEC_Result res;
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_UUID uuid = TA_HESIA_UUID;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_InitializeContext failed: 0x%x\n", res);
        goto out;
    }

    res = TEEC_OpenSession(&ctx, &sess, &uuid,
                           TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_OpenSession failed: 0x%x origin 0x%x\n",
                res, err_origin);
        TEEC_FinalizeContext(&ctx);
        goto out;
    }

    memset(&op, 0, sizeof(op));
    uint32_t cmd = 0;
    if (do_seal || do_unseal) {
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = in_buf;
        op.params[0].tmpref.size = in_len;
        op.params[1].tmpref.buffer = out_buf;
        op.params[1].tmpref.size = out_len;
        cmd = do_seal ? TA_HESIA_CMD_SEAL : TA_HESIA_CMD_UNSEAL;
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
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "TEEC_InvokeCommand failed: 0x%x origin 0x%x\n",
                res, err_origin);
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

    if (do_read_version) {
        uint64_t v = ((uint64_t)op.params[0].value.a << 32) | op.params[0].value.b;
        printf("%llu\n", (unsigned long long)v);
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    free(in_buf);
    free(out_buf);
    return 0;

out:
    free(in_buf);
    free(out_buf);
    return 1;
}
