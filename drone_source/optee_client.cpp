#include "optee_client.hpp"
#include "exceptions.hpp"
#include <fstream>
#include <string>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef HESIA_HAVE_OPTEE
#include <tee_client_api.h>
#endif

namespace hesia {

#ifdef HESIA_HAVE_OPTEE
static const TEEC_UUID kHesiaTaUuid = { 0xa17de805, 0x9dc1, 0x43ef,
    { 0x93, 0x2b, 0x91, 0xf1, 0x07, 0xca, 0xd5, 0x7b } };

static constexpr uint32_t kCmdSeal = 0x0001;
static constexpr uint32_t kCmdUnseal = 0x0002;
static constexpr uint32_t kCmdRotateKey = 0x0003;
static constexpr uint32_t kCmdWipeKey = 0x0004;
static constexpr uint32_t kCmdHkdf = 0x0005;
static constexpr uint32_t kCmdCheckVersion = 0x0006;
#endif

bool optee_available()
{
#ifdef HESIA_HAVE_OPTEE
#ifndef _WIN32
    if (geteuid() != 0 && access("/dev/tee0", R_OK | W_OK) != 0) {
        return false;
    }
#endif
    return true;
#else
    return false;
#endif
}

std::filesystem::path optee_default_sealed_puf_path()
{
    return std::filesystem::path("/etc/hesia/secure") / "hesia_seed.sealed";
}

std::filesystem::path optee_default_dilithium_sealed_path()
{
    return std::filesystem::path("/etc/hesia/secure") / "dilithium5_sk.sealed";
}

static std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw SecurityViolation("Cannot open sealed file: " + path.string());
    }

    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) {
        throw SecurityViolation("Invalid sealed file size: " + path.string());
    }
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(data.data()), size);
        if (!f) {
            throw SecurityViolation("Failed to read sealed file: " + path.string());
        }
    }
    return data;
}

#ifndef _WIN32
static void chmod_owner_only(const std::filesystem::path& path)
{
    chmod(path.c_str(), 0600);
}
#else
static void chmod_owner_only(const std::filesystem::path& path)
{
    (void)path;
}
#endif

static void ensure_parent_dir(const std::filesystem::path& path)
{
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

static void write_file(const std::filesystem::path& path, const std::vector<uint8_t>& data)
{
    ensure_parent_dir(path);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw SecurityViolation("Cannot write sealed file: " + path.string());
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    }
    f.close();
    chmod_owner_only(path);
}

#ifndef _WIN32
static void ensure_optee_access()
{
    if (geteuid() == 0) {
        return;
    }
    if (access("/dev/tee0", R_OK | W_OK) == 0) {
        return;
    }
    throw SecurityViolation("OP-TEE access requires root or /dev/tee0 access (tee group)");
}
#else
static void ensure_optee_access()
{
}
#endif

#ifdef HESIA_HAVE_OPTEE
static TEEC_Result teec_open_session(TEEC_Context* ctx, TEEC_Session* sess, uint32_t* err_origin)
{
    TEEC_Result res = TEEC_InitializeContext(NULL, ctx);
    if (res != TEEC_SUCCESS)
        return res;
    res = TEEC_OpenSession(ctx, sess, &kHesiaTaUuid,
                           TEEC_LOGIN_PUBLIC, NULL, NULL, err_origin);
    if (res != TEEC_SUCCESS) {
        TEEC_FinalizeContext(ctx);
    }
    return res;
}
#endif

std::vector<uint8_t> optee_unseal_file(const std::filesystem::path& sealed_path,
                                       size_t expected_len)
{
#ifndef HESIA_HAVE_OPTEE
    (void)sealed_path;
    (void)expected_len;
    throw SecurityViolation("OP-TEE client not available in this build");
#else
    ensure_optee_access();
    std::vector<uint8_t> sealed = read_file(sealed_path);
    if (sealed.empty()) {
        throw SecurityViolation("Sealed file is empty: " + sealed_path.string());
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(expected_len ? expected_len : sealed.size());
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = sealed.data();
        op.params[0].tmpref.size = sealed.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();

        res = TEEC_InvokeCommand(&sess, kCmdUnseal, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[1].tmpref.size);
            continue;
        }
        if (res != TEEC_SUCCESS) {
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            throw SecurityViolation("TEEC_InvokeCommand(unseal) failed: 0x" + std::to_string(res));
        }
        out.resize(op.params[1].tmpref.size);
        break;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    if (expected_len != 0 && out.size() != expected_len) {
        throw SecurityViolation("Unsealed data size mismatch");
    }

    return out;
#endif
}

bool optee_seal_file(const std::filesystem::path& sealed_path,
                     const std::vector<uint8_t>& plaintext)
{
#ifndef HESIA_HAVE_OPTEE
    (void)sealed_path;
    (void)plaintext;
    return false;
#else
    ensure_optee_access();
    if (plaintext.empty()) {
        throw SecurityViolation("Plaintext is empty for sealing");
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(plaintext.size() + 64);
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = const_cast<uint8_t*>(plaintext.data());
        op.params[0].tmpref.size = plaintext.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();

        res = TEEC_InvokeCommand(&sess, kCmdSeal, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[1].tmpref.size);
            continue;
        }
        if (res != TEEC_SUCCESS) {
            TEEC_CloseSession(&sess);
            TEEC_FinalizeContext(&ctx);
            throw SecurityViolation("TEEC_InvokeCommand(seal) failed: 0x" + std::to_string(res));
        }
        out.resize(op.params[1].tmpref.size);
        break;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    write_file(sealed_path, out);
    return true;
#endif
}

bool optee_rotate_key()
{
#ifndef HESIA_HAVE_OPTEE
    return false;
#else
    ensure_optee_access();
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

    res = TEEC_InvokeCommand(&sess, kCmdRotateKey, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(rotate) failed: 0x" + std::to_string(res));
    }
    return true;
#endif
}

bool optee_wipe_key()
{
#ifndef HESIA_HAVE_OPTEE
    return false;
#else
    ensure_optee_access();
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

    res = TEEC_InvokeCommand(&sess, kCmdWipeKey, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(wipe) failed: 0x" + std::to_string(res));
    }
    return true;
#endif
}

std::vector<uint8_t> optee_hkdf_sha3_512(const std::vector<uint8_t>& ikm,
                                         const std::vector<uint8_t>& salt,
                                         const std::vector<uint8_t>& info,
                                         size_t out_len)
{
#ifndef HESIA_HAVE_OPTEE
    (void)ikm;
    (void)salt;
    (void)info;
    (void)out_len;
    throw SecurityViolation("OP-TEE HKDF not available in this build");
#else
    ensure_optee_access();
    if (ikm.empty() || out_len == 0) {
        throw SecurityViolation("HKDF input invalid");
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(out_len);
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT);

    op.params[0].tmpref.buffer = const_cast<uint8_t*>(ikm.data());
    op.params[0].tmpref.size = ikm.size();
    op.params[1].tmpref.buffer = const_cast<uint8_t*>(salt.data());
    op.params[1].tmpref.size = salt.size();
    op.params[2].tmpref.buffer = const_cast<uint8_t*>(info.data());
    op.params[2].tmpref.size = info.size();
    op.params[3].tmpref.buffer = out.data();
    op.params[3].tmpref.size = out.size();

    res = TEEC_InvokeCommand(&sess, kCmdHkdf, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(hkdf) failed: 0x" + std::to_string(res));
    }
    out.resize(op.params[3].tmpref.size);
    return out;
#endif
}

bool optee_check_and_update_firmware_version(std::uint64_t version)
{
#ifndef HESIA_HAVE_OPTEE
    (void)version;
    return false;
#else
    ensure_optee_access();
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = static_cast<uint32_t>(version >> 32);
    op.params[0].value.b = static_cast<uint32_t>(version & 0xFFFFFFFFu);

    res = TEEC_InvokeCommand(&sess, kCmdCheckVersion, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    if (res != TEEC_SUCCESS) {
        return false;
    }
    return true;
#endif
}

} // namespace hesia
