#include "optee_client.hpp"
#include "exceptions.hpp"
#include "security_utils.hpp"
#include <fstream>
#include <string>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <array>
#include <mutex>
#include <sstream>

#ifndef _WIN32
#include <grp.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef HESIA_HAVE_OPTEE
#include <tee_client_api.h>
#endif

namespace hesia {

static constexpr uint32_t kCmdStageSlotUpdate = 0x000C;
static constexpr uint32_t kCmdCommitSlotBoot = 0x000D;
static constexpr uint32_t kCmdReadSlotMeta = 0x000E;
static constexpr uint32_t kSlotReqMagic = 0x48535531u; // HSU1
static constexpr uint32_t kSlotMetaMagic = 0x48534D31u; // HSM1

#ifdef HESIA_HAVE_OPTEE
static const TEEC_UUID kHesiaTaUuid = { 0xa17de805, 0x9dc1, 0x43ef,
    { 0x93, 0x2b, 0x91, 0xf1, 0x07, 0xca, 0xd5, 0x7b } };

static constexpr uint32_t kCmdSeal = 0x0001;
static constexpr uint32_t kCmdUnseal = 0x0002;
static constexpr uint32_t kCmdRotateKey = 0x0003;
static constexpr uint32_t kCmdWipeKey = 0x0004;
static constexpr uint32_t kCmdHkdf = 0x0005;
static constexpr uint32_t kCmdCheckVersion = 0x0006;
static constexpr uint32_t kCmdExportAttestPubkey = 0x0009;
static constexpr uint32_t kCmdSignAttestDigest = 0x000A;
static constexpr uint32_t kCmdSetSessionAuthSecret = 0x000B;
static constexpr uint32_t kCmdGetRecoveryChallenge = 0x000F;
static constexpr uint32_t kCmdImportMldsaKeyBlob = 0x0011;
static constexpr uint32_t kCmdExportMldsaPubkey = 0x0012;
static constexpr uint32_t kCmdSignMldsaPayload = 0x0013;
static constexpr uint32_t kCmdGetMldsaStatus = 0x0014;
static constexpr uint32_t kCmdGenerateMldsaKeypair = 0x0015;

static constexpr uint32_t kRecoveryChallengeMagic = 0x48535231u; // HSR1
static constexpr uint32_t kRecoveryChallengeVersion = 2u;
static constexpr std::size_t kRecoveryChallengeNonceLen = 32u;
static constexpr std::size_t kRecoveryChallengePubkeyLen = 65u;
static constexpr std::size_t kRecoveryChallengeMinWireLen =
    sizeof(std::uint32_t) * 2u + sizeof(std::uint64_t) +
    kRecoveryChallengeNonceLen + kRecoveryChallengePubkeyLen;

static TEEC_Result teec_open_session(TEEC_Context* ctx, TEEC_Session* sess, uint32_t* err_origin);
#endif

static void ensure_optee_access();

namespace {

struct SlotRequestWire {
    std::uint32_t magic = kSlotReqMagic;
    std::uint8_t slot = 0;
    std::uint8_t reserved0 = 0;
    std::uint16_t reserved1 = 0;
    std::uint64_t firmware_version = 0;
    std::uint64_t asset_version = 0;
};

struct SlotMetaWire {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint8_t initialized = 0;
    std::uint8_t active_slot = 0;
    std::uint8_t pending_slot = 0;
    std::uint8_t reserved = 0;
    std::uint64_t max_firmware_version = 0;
    std::uint64_t slot_a_firmware_version = 0;
    std::uint64_t slot_b_firmware_version = 0;
    std::uint64_t slot_a_asset_version = 0;
    std::uint64_t slot_b_asset_version = 0;
    std::uint64_t pending_firmware_version = 0;
    std::uint64_t pending_asset_version = 0;
};

static_assert(sizeof(SlotRequestWire) == 24, "Unexpected SlotRequestWire layout");
static_assert(sizeof(SlotMetaWire) == 72, "Unexpected SlotMetaWire layout");

std::mutex g_optee_auth_config_mutex;
std::filesystem::path g_optee_session_auth_secret_path = std::filesystem::path("/etc/hesia/secure") / "optee_session_auth.sealed";
bool g_optee_require_session_auth = false;

#ifdef HESIA_HAVE_OPTEE
static TEEC_Result teec_open_session_with_secret(TEEC_Context* ctx,
                                                 TEEC_Session* sess,
                                                 uint32_t* err_origin,
                                                 const std::vector<uint8_t>* auth_material);
#endif

static const char* mldsa_import_stage_label(std::uint32_t stage)
{
    switch (stage) {
    case 0u:
        return "init";
    case 1u:
        return "alloc";
    case 2u:
        return "unseal";
    case 3u:
        return "parse";
    case 4u:
        return "backend_ready";
    case 5u:
        return "selftest_sign";
    case 6u:
        return "selftest_verify";
    case 7u:
        return "persist";
    case 8u:
        return "done";
    default:
        return "unknown";
    }
}

static std::uint32_t mldsa_slot_wire_value(OpteeMldsaSlot slot)
{
    switch (slot) {
    case OpteeMldsaSlot::DroneIdentity:
        return 0u;
    case OpteeMldsaSlot::ServerIdentity:
        return 1u;
    default:
        throw SecurityViolation("Unknown OP-TEE ML-DSA slot");
    }
}

static const char* mldsa_slot_label(OpteeMldsaSlot slot)
{
    switch (slot) {
    case OpteeMldsaSlot::DroneIdentity:
        return "drone";
    case OpteeMldsaSlot::ServerIdentity:
        return "server";
    default:
        return "unknown";
    }
}

static std::string format_hex_u32(std::uint32_t value)
{
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

#ifdef HESIA_HAVE_OPTEE
static std::string format_teec_error(const char* context,
                                     TEEC_Result res,
                                     std::uint32_t err_origin,
                                     const std::filesystem::path* auth_path = nullptr)
{
    std::ostringstream oss;
    oss << context << ": res=0x" << format_hex_u32(res)
        << " origin=0x" << format_hex_u32(err_origin);
    if (auth_path && !auth_path->empty()) {
        oss << " auth_path=" << auth_path->string();
    }
    return oss.str();
}

static bool try_restore_session_auth_with_material(const std::vector<uint8_t>& auth_material,
                                                   const std::filesystem::path& auth_path)
{
    (void)auth_material;
    (void)auth_path;
    return false;
}
#endif

static std::uint32_t load_le_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

#ifdef HESIA_HAVE_OPTEE
static std::vector<std::uint8_t> fetch_attestation_pubkey_via_recovery_challenge()
{
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    std::uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed for recovery challenge fallback: 0x" +
                                format_hex_u32(res));
    }

    std::vector<std::uint8_t> out(128);
    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = out.data();
        op.params[0].tmpref.size = out.size();

        res = TEEC_InvokeCommand(&sess, kCmdGetRecoveryChallenge, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[0].tmpref.size);
            continue;
        }

        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);

        if (res != TEEC_SUCCESS) {
            throw SecurityViolation("TEEC_InvokeCommand(get_recovery_challenge) failed: 0x" +
                                    format_hex_u32(res) +
                                    " origin=0x" + format_hex_u32(err_origin));
        }

        out.resize(op.params[0].tmpref.size);
        if (out.size() < kRecoveryChallengeMinWireLen) {
            throw SecurityViolation("Recovery challenge too short for attestation key fallback");
        }

        const std::uint32_t magic = load_le_u32(out.data());
        const std::uint32_t version = load_le_u32(out.data() + 4);
        if (magic != kRecoveryChallengeMagic || version != kRecoveryChallengeVersion) {
            throw SecurityViolation("Recovery challenge attestation fallback returned invalid header");
        }

        const std::size_t pubkey_offset =
            sizeof(std::uint32_t) * 2u + sizeof(std::uint64_t) + kRecoveryChallengeNonceLen;
        using Diff = std::vector<std::uint8_t>::difference_type;
        return std::vector<std::uint8_t>(out.begin() + static_cast<Diff>(pubkey_offset),
                                         out.begin() + static_cast<Diff>(pubkey_offset + kRecoveryChallengePubkeyLen));
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    throw SecurityViolation("Recovery challenge attestation fallback exceeded retry budget");
}
#endif

#ifndef _WIN32
static const std::filesystem::path kRuntimeSecureDir("/etc/hesia/secure");
static constexpr const char* kRuntimeSecureGroup = "hesia";

static bool is_runtime_secure_dir(const std::filesystem::path& path)
{
    return path.lexically_normal() == kRuntimeSecureDir.lexically_normal();
}

static bool lives_in_runtime_secure_dir(const std::filesystem::path& path)
{
    return path.parent_path().lexically_normal() == kRuntimeSecureDir.lexically_normal();
}

static gid_t runtime_secure_group_id()
{
    static gid_t cached_gid = static_cast<gid_t>(-1);
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        if (struct group* gr = getgrnam(kRuntimeSecureGroup)) {
            cached_gid = gr->gr_gid;
        }
    }
    return cached_gid;
}

static void maybe_apply_runtime_secure_dir_permissions(const std::filesystem::path& dir)
{
    if (geteuid() != 0 || !is_runtime_secure_dir(dir)) {
        return;
    }
    const gid_t gid = runtime_secure_group_id();
    if (gid == static_cast<gid_t>(-1)) {
        return;
    }
    (void)chown(dir.c_str(), 0, gid);
    (void)chmod(dir.c_str(), 0750);
}

static void apply_sealed_file_permissions(const std::filesystem::path& path)
{
    if (geteuid() == 0 && lives_in_runtime_secure_dir(path)) {
        const gid_t gid = runtime_secure_group_id();
        if (gid != static_cast<gid_t>(-1)) {
            (void)chown(path.c_str(), 0, gid);
            (void)chmod(path.c_str(), 0640);
            return;
        }
    }
    (void)chmod(path.c_str(), 0600);
}
#endif

static bool looks_like_hesia_sealed_blob(const std::vector<uint8_t>& blob)
{
    return blob.size() >= 33 &&
           blob[0] == static_cast<uint8_t>('H') &&
           blob[1] == static_cast<uint8_t>('E') &&
           blob[2] == static_cast<uint8_t>('S') &&
           blob[3] == static_cast<uint8_t>('1');
}

static std::vector<uint8_t> read_session_auth_material_from_path(const std::filesystem::path& path,
                                                                 bool required)
{
    if (path.empty()) {
        if (required) {
            throw SecurityViolation("OP-TEE session authentication path not configured");
        }
        return {};
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        if (required) {
            throw SecurityViolation("Cannot open OP-TEE session authentication secret: " + path.string());
        }
        return {};
    }

    std::vector<uint8_t> material((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    if (material.empty()) {
        throw SecurityViolation("OP-TEE session authentication material is empty");
    }
    (void)SecureMemory::protect(material);
    if (!looks_like_hesia_sealed_blob(material)) {
        SecureMemory::zeroize(material);
        throw SecurityViolation("OP-TEE session authentication material must be a HESIA sealed blob");
    }
    return material;
}

static std::filesystem::path get_session_auth_secret_path_locked()
{
    const char* env = std::getenv("HESIA_OPTEE_SESSION_AUTH_PATH");
    if (env && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return g_optee_session_auth_secret_path;
}

static std::vector<uint8_t> sealed_blob_aad_for_path(const std::filesystem::path& path)
{
    const std::string label = path.filename().string();
    if (label.empty()) {
        return {};
    }
    std::string structured;
    if (label == "optee_session_auth.sealed") {
        structured = "HESIA|TA|session_auth|v1";
    } else if (label == "dilithium5_sk.sealed") {
        structured = "HESIA|TA|mldsa_drone|v1";
    } else if (label == "server_secret.bin.sealed") {
        structured = "HESIA|TA|mldsa_server|v1";
    } else {
        structured = "HESIA|TA|blob|" + label + "|v1";
    }
    return std::vector<uint8_t>(structured.begin(), structured.end());
}

static std::vector<uint8_t> legacy_named_blob_aad_for_path(const std::filesystem::path& path)
{
    const std::string label = path.filename().string();
    if (label.empty()) {
        return {};
    }
    return std::vector<uint8_t>(label.begin(), label.end());
}

static bool is_legacy_sealed_blob_name(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    return name == "hesia_seed.sealed" ||
           name == "dilithium5_sk.sealed" ||
           name == "server_secret.bin.sealed";
}

#ifdef HESIA_HAVE_OPTEE
static bool is_legacy_unseal_retryable_result(TEEC_Result res)
{
    return res == TEEC_ERROR_BAD_PARAMETERS ||
           res == static_cast<TEEC_Result>(TEE_ERROR_MAC_INVALID);
}
#endif

#ifdef HESIA_HAVE_OPTEE
static TEEC_Result teec_open_session_with_secret(TEEC_Context* ctx,
                                                 TEEC_Session* sess,
                                                 uint32_t* err_origin,
                                                 const std::vector<uint8_t>* auth_material)
{
    TEEC_Result res = TEEC_InitializeContext(NULL, ctx);
    if (res != TEEC_SUCCESS) {
        return res;
    }

    TEEC_Operation open_op;
    TEEC_Operation* open_op_ptr = nullptr;
    std::memset(&open_op, 0, sizeof(open_op));

    if (auth_material && !auth_material->empty()) {
        open_op_ptr = &open_op;
        open_op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                              TEEC_NONE,
                                              TEEC_NONE,
                                              TEEC_NONE);
        open_op.params[0].tmpref.buffer = const_cast<uint8_t*>(auth_material->data());
        open_op.params[0].tmpref.size = auth_material->size();
    }

#ifndef TEEC_LOGIN_APPLICATION
#error "TEEC_LOGIN_APPLICATION is required for HESIA OP-TEE client authentication"
#endif
    const bool secret_bound_session = auth_material && !auth_material->empty();
    uint32_t login_method = secret_bound_session
        ? TEEC_LOGIN_PUBLIC
        : TEEC_LOGIN_APPLICATION;

    res = TEEC_OpenSession(ctx, sess, &kHesiaTaUuid,
                           login_method, NULL, open_op_ptr, err_origin);
    if (res != TEEC_SUCCESS) {
        TEEC_FinalizeContext(ctx);
    }
    return res;
}
#endif

#ifndef _WIN32
static void validate_session_auth_secret_node(const std::filesystem::path& path)
{
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0) {
        const int saved_errno = errno;
        throw SecurityViolation("Cannot stat OP-TEE session authentication secret: " +
                                path.string() + ": " + std::strerror(saved_errno));
    }
    if (S_ISLNK(st.st_mode)) {
        throw SecurityViolation("OP-TEE session authentication secret must not be a symlink");
    }
    if (!S_ISREG(st.st_mode)) {
        throw SecurityViolation("OP-TEE session authentication secret must be a regular file");
    }
    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        throw SecurityViolation("OP-TEE session authentication secret must not be group/other writable");
    }
}
#else
static void validate_session_auth_secret_node(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        throw SecurityViolation("OP-TEE session authentication secret missing or invalid: " + path.string());
    }
}
#endif

static void validate_slot_request(OpteeSlotId slot,
                                  std::uint64_t firmware_version,
                                  std::uint64_t asset_version)
{
    if (slot != OpteeSlotId::SlotA && slot != OpteeSlotId::SlotB) {
        throw SecurityViolation("Invalid OP-TEE slot identifier");
    }
    if (firmware_version == 0) {
        throw SecurityViolation("Firmware version must be non-zero for slot metadata");
    }
    if (asset_version == 0) {
        throw SecurityViolation("Asset manifest version must be non-zero for slot metadata");
    }
}

static bool invoke_slot_command(uint32_t cmd_id,
                                OpteeSlotId slot,
                                std::uint64_t firmware_version,
                                std::uint64_t asset_version)
{
#ifndef HESIA_HAVE_OPTEE
    (void)cmd_id;
    (void)slot;
    (void)firmware_version;
    (void)asset_version;
    return false;
#else
    ensure_optee_access();
    validate_slot_request(slot, firmware_version, asset_version);

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    std::uint32_t err_origin = 0;
    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    SlotRequestWire req{};
    req.slot = static_cast<std::uint8_t>(slot);
    req.firmware_version = firmware_version;
    req.asset_version = asset_version;

    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_NONE,
                                     TEEC_NONE,
                                     TEEC_NONE);
    op.params[0].tmpref.buffer = &req;
    op.params[0].tmpref.size = sizeof(req);

    res = TEEC_InvokeCommand(&sess, cmd_id, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    return res == TEEC_SUCCESS;
#endif
}

} // namespace

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

void optee_set_session_auth_secret_path(const std::filesystem::path& path)
{
    std::lock_guard<std::mutex> lock(g_optee_auth_config_mutex);
    g_optee_session_auth_secret_path = path;
}

void optee_require_session_auth(bool required)
{
    std::lock_guard<std::mutex> lock(g_optee_auth_config_mutex);
    g_optee_require_session_auth = required;
}

void optee_require_session_auth_ready_or_throw()
{
    std::filesystem::path secret_path;
    bool require_auth = false;
    {
        std::lock_guard<std::mutex> lock(g_optee_auth_config_mutex);
        secret_path = get_session_auth_secret_path_locked();
        require_auth = g_optee_require_session_auth;
    }

    if (!require_auth) {
        return;
    }

    if (!optee_available()) {
        throw SecurityViolation("OP-TEE authenticated session required but OP-TEE is unavailable");
    }

    validate_session_auth_secret_node(secret_path);
    std::vector<uint8_t> auth_material = read_session_auth_material_from_path(secret_path, true);

#ifndef HESIA_HAVE_OPTEE
    SecureMemory::zeroize(auth_material);
    throw SecurityViolation("OP-TEE session authentication required but this build has no OP-TEE support");
#else
    ensure_optee_access();
    TEEC_Context ctx;
    TEEC_Session sess;
    uint32_t err_origin = 0;
    TEEC_Result res = teec_open_session_with_secret(&ctx, &sess, &err_origin, &auth_material);
    if (res == TEEC_ERROR_ITEM_NOT_FOUND && looks_like_hesia_sealed_blob(auth_material)) {
        if (try_restore_session_auth_with_material(auth_material, secret_path)) {
            err_origin = 0;
        res = teec_open_session_with_secret(&ctx, &sess, &err_origin, &auth_material);
        }
    }
    SecureMemory::zeroize(auth_material);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation(format_teec_error(
            "OP-TEE authenticated session not ready or secret not provisioned",
            res, err_origin, &secret_path));
    }
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
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
    (void)SecureMemory::protect(data);
    return data;
}

#ifndef _WIN32
static void chmod_owner_only(const std::filesystem::path& path)
{
    apply_sealed_file_permissions(path);
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
#ifndef _WIN32
        maybe_apply_runtime_secure_dir_permissions(parent);
#endif
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
    std::filesystem::path secret_path;
    bool require_auth = false;
    {
        std::lock_guard<std::mutex> lock(g_optee_auth_config_mutex);
        secret_path = get_session_auth_secret_path_locked();
        require_auth = g_optee_require_session_auth;
    }

    std::vector<uint8_t> auth_material = read_session_auth_material_from_path(secret_path, require_auth);
    TEEC_Result res = teec_open_session_with_secret(ctx, sess, err_origin,
                                                    auth_material.empty() ? nullptr : &auth_material);
    if (res == TEEC_ERROR_ITEM_NOT_FOUND &&
        !auth_material.empty() &&
        looks_like_hesia_sealed_blob(auth_material) &&
        try_restore_session_auth_with_material(auth_material, secret_path)) {
        res = teec_open_session_with_secret(ctx, sess, err_origin, &auth_material);
    }
    SecureMemory::zeroize(auth_material);
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
    const std::vector<uint8_t> aad = sealed_blob_aad_for_path(sealed_path);

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(expected_len ? expected_len : sealed.size());
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     aad.empty() ? TEEC_NONE : TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_NONE);

    auto invoke_unseal = [&](const std::vector<uint8_t>* aad_override,
                             std::vector<uint8_t>& output) -> TEEC_Result {
        memset(&op, 0, sizeof(op));
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT,
                                         aad_override && !aad_override->empty() ? TEEC_MEMREF_TEMP_INPUT : TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = sealed.data();
        op.params[0].tmpref.size = sealed.size();
        op.params[1].tmpref.buffer = output.data();
        op.params[1].tmpref.size = output.size();
        if (aad_override && !aad_override->empty()) {
            op.params[2].tmpref.buffer = const_cast<uint8_t*>(aad_override->data());
            op.params[2].tmpref.size = aad_override->size();
        }
        return TEEC_InvokeCommand(&sess, kCmdUnseal, &op, &err_origin);
    };

    bool migrated_legacy_blob = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        res = invoke_unseal(&aad, out);
        if (is_legacy_unseal_retryable_result(res) && !aad.empty()) {
            const std::vector<uint8_t> basename_aad = legacy_named_blob_aad_for_path(sealed_path);
            if (!basename_aad.empty()) {
                std::vector<uint8_t> legacy_out = out;
                TEEC_Result legacy_res = invoke_unseal(&basename_aad, legacy_out);
                if (legacy_res == TEEC_ERROR_SHORT_BUFFER) {
                    legacy_out.resize(op.params[1].tmpref.size);
                    legacy_res = invoke_unseal(&basename_aad, legacy_out);
                }
                if (legacy_res == TEEC_SUCCESS) {
                    legacy_out.resize(op.params[1].tmpref.size);
                    out = legacy_out;
                    res = TEEC_SUCCESS;
                    migrated_legacy_blob = true;
                } else if (is_legacy_unseal_retryable_result(legacy_res) &&
                           is_legacy_sealed_blob_name(sealed_path)) {
                    legacy_out = out;
                    legacy_res = invoke_unseal(nullptr, legacy_out);
                    if (legacy_res == TEEC_ERROR_SHORT_BUFFER) {
                        legacy_out.resize(op.params[1].tmpref.size);
                        legacy_res = invoke_unseal(nullptr, legacy_out);
                    }
                    if (legacy_res == TEEC_SUCCESS) {
                        legacy_out.resize(op.params[1].tmpref.size);
                        out = legacy_out;
                        res = TEEC_SUCCESS;
                        migrated_legacy_blob = true;
                    }
                }
            }
        }

        op.params[0].tmpref.buffer = sealed.data();
        op.params[0].tmpref.size = sealed.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();
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
    (void)SecureMemory::protect(out);

    if (migrated_legacy_blob) {
        try {
            optee_seal_file(sealed_path, out);
        } catch (...) {
            // Best-effort migration only: some deployed secure-world runtimes may
            // still reject AAD-aware reseal even though legacy unseal succeeds.
            // Keep availability and continue using the legacy blob until the TA
            // can be upgraded cleanly on the target.
        }
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
    const std::vector<uint8_t> aad = sealed_blob_aad_for_path(sealed_path);

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(plaintext.size() + 64);
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     aad.empty() ? TEEC_NONE : TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = const_cast<uint8_t*>(plaintext.data());
        op.params[0].tmpref.size = plaintext.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();
        if (!aad.empty()) {
            op.params[2].tmpref.buffer = const_cast<uint8_t*>(aad.data());
            op.params[2].tmpref.size = aad.size();
        }

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

std::vector<uint8_t> optee_get_attestation_public_key()
{
#ifndef HESIA_HAVE_OPTEE
    throw SecurityViolation("OP-TEE attestation public key export not available in this build");
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

    std::vector<uint8_t> out(65);
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = out.data();
        op.params[0].tmpref.size = out.size();

        res = TEEC_InvokeCommand(&sess, kCmdExportAttestPubkey, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[0].tmpref.size);
            continue;
        }
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        if (res != TEEC_SUCCESS) {
            return fetch_attestation_pubkey_via_recovery_challenge();
        }
        out.resize(op.params[0].tmpref.size);
        if (out.size() != 65) {
            throw SecurityViolation("Unexpected TEE attestation public key size");
        }
        return out;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    throw SecurityViolation("TEE attestation public key export exceeded retry budget");
#endif
}

std::vector<uint8_t> optee_sign_attestation_digest(const std::vector<uint8_t>& digest)
{
#ifndef HESIA_HAVE_OPTEE
    (void)digest;
    throw SecurityViolation("OP-TEE attestation signing not available in this build");
#else
    ensure_optee_access();
    if (digest.size() != 32) {
        throw SecurityViolation("Invalid digest size for TEE attestation signature");
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(64);
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = const_cast<uint8_t*>(digest.data());
        op.params[0].tmpref.size = digest.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();

        res = TEEC_InvokeCommand(&sess, kCmdSignAttestDigest, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[1].tmpref.size);
            continue;
        }
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        if (res != TEEC_SUCCESS) {
            throw SecurityViolation("TEEC_InvokeCommand(sign_attest_digest) failed: 0x" + std::to_string(res));
        }
        out.resize(op.params[1].tmpref.size);
        if (out.size() != 64) {
            throw SecurityViolation("Unexpected TEE attestation signature size");
        }
        return out;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    throw SecurityViolation("TEE attestation signing exceeded retry budget");
#endif
}

bool optee_provision_session_auth_secret(const std::vector<uint8_t>& secret)
{
#ifndef HESIA_HAVE_OPTEE
    (void)secret;
    return false;
#else
    ensure_optee_access();
    if (secret.size() != 32) {
        throw SecurityViolation("OP-TEE session authentication secret must be exactly 32 bytes");
    }
    throw SecurityViolation("Insecure OP-TEE bootstrap provisioning is disabled; use recovery_challenge + recover_session_auth");
#endif
}

bool optee_restore_session_auth_from_blob(const std::vector<uint8_t>& sealed_or_raw_secret)
{
#ifndef HESIA_HAVE_OPTEE
    (void)sealed_or_raw_secret;
    return false;
#else
    ensure_optee_access();
    if (sealed_or_raw_secret.empty()) {
        throw SecurityViolation("OP-TEE session authentication restore material is empty");
    }
    throw SecurityViolation("Insecure OP-TEE bootstrap restore is disabled; use recovery_challenge + recover_session_auth");
#endif
}

bool optee_rotate_session_auth_secret(const std::vector<uint8_t>& new_secret)
{
#ifndef HESIA_HAVE_OPTEE
    (void)new_secret;
    return false;
#else
    ensure_optee_access();
    if (new_secret.size() != 32) {
        throw SecurityViolation("New OP-TEE session authentication secret must be exactly 32 bytes");
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_NONE,
                                     TEEC_NONE,
                                     TEEC_NONE);
    op.params[0].tmpref.buffer = const_cast<uint8_t*>(new_secret.data());
    op.params[0].tmpref.size = new_secret.size();

    res = TEEC_InvokeCommand(&sess, kCmdSetSessionAuthSecret, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(set_session_auth_secret) failed: 0x" + std::to_string(res));
    }
    return true;
#endif
}

bool optee_import_mldsa_key_from_sealed_blob(const std::filesystem::path& sealed_path,
                                             OpteeMldsaSlot slot)
{
#ifndef HESIA_HAVE_OPTEE
    (void)sealed_path;
    return false;
#else
    ensure_optee_access();
    std::vector<uint8_t> sealed = read_file(sealed_path);
    if (sealed.empty()) {
        throw SecurityViolation("ML-DSA sealed key blob is empty: " + sealed_path.string());
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_VALUE_OUTPUT,
                                     TEEC_VALUE_INPUT,
                                     TEEC_NONE);
    op.params[0].tmpref.buffer = sealed.data();
    op.params[0].tmpref.size = sealed.size();
    op.params[2].value.a = mldsa_slot_wire_value(slot);

    res = TEEC_InvokeCommand(&sess, kCmdImportMldsaKeyBlob, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);

    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(import_mldsa_key_blob) failed for slot " +
                                std::string(mldsa_slot_label(slot)) + ": 0x" +
                                format_hex_u32(res) +
                                " origin=0x" + format_hex_u32(err_origin) +
                                " stage=" + std::to_string(op.params[1].value.a) +
                                std::string(" (") + mldsa_import_stage_label(op.params[1].value.a) + ")" +
                                " ta_res=0x" + format_hex_u32(op.params[1].value.b));
    }
    if (op.params[1].value.b != TEEC_SUCCESS) {
        throw SecurityViolation("TEE ML-DSA import failed for slot " +
                                std::string(mldsa_slot_label(slot)) + ": stage=" +
                                std::to_string(op.params[1].value.a) +
                                std::string(" (") + mldsa_import_stage_label(op.params[1].value.a) + ")" +
                                " ta_res=0x" + format_hex_u32(op.params[1].value.b));
    }
    return true;
#endif
}

std::vector<uint8_t> optee_get_mldsa_public_key(OpteeMldsaSlot slot)
{
#ifndef HESIA_HAVE_OPTEE
    throw SecurityViolation("OP-TEE ML-DSA public key export not available in this build");
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

    std::vector<uint8_t> out(2592);
    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_VALUE_INPUT,
                                     TEEC_NONE,
                                     TEEC_NONE);
    op.params[1].value.a = mldsa_slot_wire_value(slot);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = out.data();
        op.params[0].tmpref.size = out.size();
        res = TEEC_InvokeCommand(&sess, kCmdExportMldsaPubkey, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[0].tmpref.size);
            continue;
        }
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        if (res != TEEC_SUCCESS) {
            throw SecurityViolation("TEEC_InvokeCommand(export_mldsa_pubkey) failed for slot " +
                                    std::string(mldsa_slot_label(slot)) +
                                    ": 0x" + std::to_string(res));
        }
        out.resize(op.params[0].tmpref.size);
        if (out.empty()) {
            throw SecurityViolation("TEE ML-DSA public key export returned empty key for slot " +
                                    std::string(mldsa_slot_label(slot)));
        }
        return out;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    throw SecurityViolation("TEE ML-DSA public key export exceeded retry budget for slot " +
                            std::string(mldsa_slot_label(slot)));
#endif
}

std::vector<uint8_t> optee_sign_mldsa_payload(const std::vector<uint8_t>& payload,
                                              OpteeMldsaSlot slot)
{
#ifndef HESIA_HAVE_OPTEE
    (void)payload;
    throw SecurityViolation("OP-TEE ML-DSA signing not available in this build");
#else
    ensure_optee_access();
    if (payload.empty() || payload.size() > 1024 * 1024) {
        throw SecurityViolation("Invalid ML-DSA payload size for TEE signing");
    }

    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    std::vector<uint8_t> out(5120);
    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                     TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_VALUE_INPUT,
                                     TEEC_NONE);
    op.params[2].value.a = mldsa_slot_wire_value(slot);

    for (int attempt = 0; attempt < 2; ++attempt) {
        op.params[0].tmpref.buffer = const_cast<uint8_t*>(payload.data());
        op.params[0].tmpref.size = payload.size();
        op.params[1].tmpref.buffer = out.data();
        op.params[1].tmpref.size = out.size();
        res = TEEC_InvokeCommand(&sess, kCmdSignMldsaPayload, &op, &err_origin);
        if (res == TEEC_ERROR_SHORT_BUFFER) {
            out.resize(op.params[1].tmpref.size);
            continue;
        }
        TEEC_CloseSession(&sess);
        TEEC_FinalizeContext(&ctx);
        if (res != TEEC_SUCCESS) {
            throw SecurityViolation("TEEC_InvokeCommand(sign_mldsa_payload) failed for slot " +
                                    std::string(mldsa_slot_label(slot)) +
                                    ": 0x" + std::to_string(res));
        }
        out.resize(op.params[1].tmpref.size);
        if (out.empty()) {
            throw SecurityViolation("TEE ML-DSA signing returned empty signature for slot " +
                                    std::string(mldsa_slot_label(slot)));
        }
        return out;
    }

    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    throw SecurityViolation("TEE ML-DSA signing exceeded retry budget for slot " +
                            std::string(mldsa_slot_label(slot)));
#endif
}

bool optee_mldsa_signing_ready(OpteeMldsaSlot slot)
{
#ifndef HESIA_HAVE_OPTEE
    return false;
#else
    try {
        ensure_optee_access();
    } catch (...) {
        return false;
    }
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    uint32_t err_origin = 0;

    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        return false;
    }

    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT,
                                     TEEC_VALUE_INPUT,
                                     TEEC_NONE,
                                     TEEC_NONE);
    op.params[1].value.a = mldsa_slot_wire_value(slot);
    res = TEEC_InvokeCommand(&sess, kCmdGetMldsaStatus, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    if (res != TEEC_SUCCESS) {
        return false;
    }
    return op.params[0].value.a == 1u && op.params[0].value.b == 1u;
#endif
}

OpteeSlotId optee_slot_id_from_string(const std::string& slot)
{
    if (slot == "A" || slot == "a" || slot == "slot_a" || slot == "slot-a") {
        return OpteeSlotId::SlotA;
    }
    if (slot == "B" || slot == "b" || slot == "slot_b" || slot == "slot-b") {
        return OpteeSlotId::SlotB;
    }
    return OpteeSlotId::Invalid;
}

std::string optee_slot_id_to_string(OpteeSlotId slot)
{
    switch (slot) {
    case OpteeSlotId::SlotA:
        return "A";
    case OpteeSlotId::SlotB:
        return "B";
    default:
        return "invalid";
    }
}

bool optee_stage_slot_update(OpteeSlotId slot,
                             std::uint64_t firmware_version,
                             std::uint64_t asset_version)
{
    return invoke_slot_command(kCmdStageSlotUpdate, slot, firmware_version, asset_version);
}

bool optee_commit_slot_boot(OpteeSlotId slot,
                            std::uint64_t firmware_version,
                            std::uint64_t asset_version)
{
    return invoke_slot_command(kCmdCommitSlotBoot, slot, firmware_version, asset_version);
}

OpteeSlotMeta optee_read_slot_meta()
{
    OpteeSlotMeta meta;
#ifndef HESIA_HAVE_OPTEE
    return meta;
#else
    ensure_optee_access();
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_Operation op;
    std::uint32_t err_origin = 0;
    TEEC_Result res = teec_open_session(&ctx, &sess, &err_origin);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_OpenSession failed: 0x" + std::to_string(res));
    }

    SlotMetaWire wire{};
    std::memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                     TEEC_NONE,
                                     TEEC_NONE,
                                     TEEC_NONE);
    op.params[0].tmpref.buffer = &wire;
    op.params[0].tmpref.size = sizeof(wire);

    res = TEEC_InvokeCommand(&sess, kCmdReadSlotMeta, &op, &err_origin);
    TEEC_CloseSession(&sess);
    TEEC_FinalizeContext(&ctx);
    if (res != TEEC_SUCCESS) {
        throw SecurityViolation("TEEC_InvokeCommand(read_slot_meta) failed: 0x" + std::to_string(res));
    }
    if (op.params[0].tmpref.size != sizeof(wire) || wire.magic != kSlotMetaMagic) {
        throw SecurityViolation("Invalid OP-TEE slot metadata payload");
    }

    meta.initialized = (wire.initialized != 0);
    meta.active_slot = static_cast<OpteeSlotId>(wire.active_slot);
    meta.pending_slot = static_cast<OpteeSlotId>(wire.pending_slot);
    meta.max_firmware_version = wire.max_firmware_version;
    meta.slot_a_firmware_version = wire.slot_a_firmware_version;
    meta.slot_b_firmware_version = wire.slot_b_firmware_version;
    meta.slot_a_asset_version = wire.slot_a_asset_version;
    meta.slot_b_asset_version = wire.slot_b_asset_version;
    meta.pending_firmware_version = wire.pending_firmware_version;
    meta.pending_asset_version = wire.pending_asset_version;
    return meta;
#endif
}

} // namespace hesia
