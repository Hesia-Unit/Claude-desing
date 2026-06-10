#include "hesia_drone.hpp"
#include "exceptions.hpp"
#include "optee_client.hpp"
#include "security_utils.hpp"
#include "server_pubkey.h"
#include <iostream>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/kdf.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <array>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

// ✅ Inclure le fichier de données de la clé publique
#include "mldsa87_public_key.h"
#include "kdf_sp800_108.hpp"

namespace hesia {

static std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
parse_dilithium_keys(const std::vector<uint8_t>& blob);
static void wipe_sensitive_vector(std::vector<uint8_t>& data);

namespace {

static std::filesystem::path get_self_path() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        throw std::runtime_error("GetModuleFileNameA failed");
    }
    return std::filesystem::path(std::string(buf, buf + len));
#else
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
    std::array<char, PATH_MAX> buf{};
    const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n < 0) {
        throw std::runtime_error("readlink(/proc/self/exe) failed");
    }
    buf[static_cast<size_t>(n)] = '\0';
    return std::filesystem::path(std::string(buf.data()));
#endif
}

static std::vector<uint8_t> read_file_binary(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open file: " + path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

static bool looks_base64(const std::vector<uint8_t>& data) {
    for (uint8_t c : data) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            continue;
        }
        return false;
    }
    return !data.empty();
}

static std::vector<uint8_t> base64_decode(const std::vector<uint8_t>& data) {
    std::string compact;
    compact.reserve(data.size());
    for (uint8_t c : data) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') continue;
        compact.push_back(static_cast<char>(c));
    }
    if (compact.empty()) return {};
    if ((compact.size() % 4) != 0) {
        throw std::runtime_error("Base64 decode failed: length not multiple of 4");
    }

    size_t padding = 0;
    if (!compact.empty() && compact.back() == '=') {
        padding++;
        if (compact.size() >= 2 && compact[compact.size() - 2] == '=') {
            padding++;
        }
    }

    const int len = static_cast<int>(compact.size());
    const int out_len = (len * 3) / 4;
    std::vector<uint8_t> out(static_cast<size_t>(out_len));
    int n = EVP_DecodeBlock(out.data(),
                            reinterpret_cast<const unsigned char*>(compact.data()),
                            len);
    if (n < 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    if (padding > 2 || n < static_cast<int>(padding)) {
        throw std::runtime_error("Base64 decode failed: invalid padding");
    }
    n -= static_cast<int>(padding);
    out.resize(static_cast<size_t>(n));
    return out;
}

static bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return false;
    }
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

static bool verify_ed25519_signature(const std::vector<uint8_t>& data,
                                     const std::vector<uint8_t>& sig,
                                     const std::filesystem::path& pubkey_path) {
    if (sig.empty()) return false;
    FILE* fp = fopen(pubkey_path.string().c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open public key: " + pubkey_path.string());
    }
    EVP_PKEY* pkey = PEM_read_PUBKEY(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!pkey) {
        throw std::runtime_error("Failed to read public key: " + pubkey_path.string());
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    int ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey);
    if (ok != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_DigestVerifyInit failed");
    }
    ok = EVP_DigestVerify(ctx, sig.data(), sig.size(), data.data(), data.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok == 1;
}

static std::uint64_t read_firmware_version(const SecurityPolicy& policy) {
    if (!policy.firmware_version_file.empty()) {
        std::ifstream f(policy.firmware_version_file);
        if (!f.is_open()) {
            throw std::runtime_error("Cannot open firmware_version_file: " + policy.firmware_version_file);
        }
        std::uint64_t v = 0;
        f >> v;
        if (v > 0) return v;
    }
    return policy.firmware_version;
}

static std::vector<uint8_t> derive_oem_kdf_key(const SecurityPolicy& policy) {
    if (policy.oem_k1_path.empty() || policy.oem_k2_path.empty()) {
        return {};
    }
    std::vector<uint8_t> k1 = read_file_binary(policy.oem_k1_path);
    std::vector<uint8_t> k2 = read_file_binary(policy.oem_k2_path);
    if (k1.empty() || k2.empty()) {
        return {};
    }
    std::vector<uint8_t> key_material;
    key_material.reserve(k1.size() + k2.size());
    key_material.insert(key_material.end(), k1.begin(), k1.end());
    key_material.insert(key_material.end(), k2.begin(), k2.end());
    std::vector<uint8_t> ctx(policy.oem_kdf_context.begin(), policy.oem_kdf_context.end());
    return kdf_sp800_108_hmac_sha256(key_material, policy.oem_kdf_label, ctx, 32);
}

static std::vector<uint8_t> compute_policy_hash_or_throw(const SecurityPolicy& policy) {
    const std::filesystem::path policy_path(policy.policy_path);
    std::vector<uint8_t> policy_bytes = read_file_binary(policy_path);
    if (policy_bytes.empty()) {
        throw SecurityViolation("Policy file is empty: " + policy_path.string());
    }
    return hash_data(policy_bytes);
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(start, end - start);
}

static bool parse_bool_like(const std::string& value) {
    std::string t = value;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

static std::unordered_map<std::string, std::string>
parse_kv_manifest_or_throw(const std::vector<uint8_t>& raw) {
    if (raw.empty() || raw.size() > (64u * 1024u)) {
        throw SecurityViolation("Measured boot payload size invalid");
    }
    if (std::find(raw.begin(), raw.end(), static_cast<uint8_t>('\0')) != raw.end()) {
        throw SecurityViolation("Measured boot payload must be text-based");
    }

    std::string text(raw.begin(), raw.end());
    std::istringstream iss(text);
    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(iss, line)) {
        std::string t = trim_copy(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        const size_t pos = t.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trim_copy(t.substr(0, pos));
        std::string value = trim_copy(t.substr(pos + 1));
        if (!key.empty()) {
            kv[key] = value;
        }
    }
    return kv;
}

static std::vector<uint8_t> hex_to_bytes_or_throw(const std::string& hex) {
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
        throw SecurityViolation("Invalid hexadecimal character in measured boot manifest");
    };

    std::string compact;
    compact.reserve(hex.size());
    for (unsigned char c : hex) {
        if (std::isspace(c) == 0) {
            compact.push_back(static_cast<char>(c));
        }
    }
    if ((compact.size() % 2) != 0) {
        throw SecurityViolation("Measured boot hex value has odd length");
    }

    std::vector<uint8_t> out(compact.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>((nibble(compact[2 * i]) << 4) |
                                      nibble(compact[2 * i + 1]));
    }
    return out;
}

static std::vector<uint8_t> sha256_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw SecurityViolation("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw SecurityViolation("EVP sha256 init/update failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != SHA256_DIGEST_LENGTH) {
        EVP_MD_CTX_free(ctx);
        throw SecurityViolation("EVP sha256 final failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

static std::vector<uint8_t> ecdsa_rs_to_der(const std::vector<uint8_t>& signature) {
    if (signature.size() != 64) {
        throw SecurityViolation("Invalid raw ECDSA signature size");
    }

    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (!sig) {
        throw SecurityViolation("ECDSA_SIG_new failed");
    }

    BIGNUM* r = BN_bin2bn(signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature.data() + 32, 32, nullptr);
    if (!r || !s || ECDSA_SIG_set0(sig, r, s) != 1) {
        if (r) BN_free(r);
        if (s) BN_free(s);
        ECDSA_SIG_free(sig);
        throw SecurityViolation("ECDSA_SIG_set0 failed");
    }

    const int der_len = i2d_ECDSA_SIG(sig, nullptr);
    if (der_len <= 0) {
        ECDSA_SIG_free(sig);
        throw SecurityViolation("i2d_ECDSA_SIG size failed");
    }

    std::vector<uint8_t> der(static_cast<size_t>(der_len));
    unsigned char* der_ptr = der.data();
    if (i2d_ECDSA_SIG(sig, &der_ptr) != der_len) {
        ECDSA_SIG_free(sig);
        throw SecurityViolation("i2d_ECDSA_SIG failed");
    }

    ECDSA_SIG_free(sig);
    return der;
}

static EVP_PKEY* load_p256_pubkey_from_uncompressed(const std::vector<uint8_t>& pubkey) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    if (!ctx) {
        throw SecurityViolation("EVP_PKEY_CTX_new_from_name failed");
    }
    if (EVP_PKEY_fromdata_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw SecurityViolation("EVP_PKEY_fromdata_init failed");
    }

    OSSL_PARAM params[3];
    params[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_PKEY_PARAM_GROUP_NAME,
        const_cast<char*>("prime256v1"),
        0);
    params[1] = OSSL_PARAM_construct_octet_string(
        OSSL_PKEY_PARAM_PUB_KEY,
        const_cast<unsigned char*>(pubkey.data()),
        pubkey.size());
    params[2] = OSSL_PARAM_construct_end();

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw SecurityViolation("EVP_PKEY_fromdata failed");
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static bool verify_p256_attestation_signature_local(const std::vector<uint8_t>& pubkey,
                                                    const std::vector<uint8_t>& digest,
                                                    const std::vector<uint8_t>& signature) {
    if (pubkey.size() != 65 || pubkey[0] != 0x04 || signature.size() != 64 || digest.empty()) {
        return false;
    }

    EVP_PKEY* pkey = load_p256_pubkey_from_uncompressed(pubkey);
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw SecurityViolation("EVP_PKEY_CTX_new failed");
    }

    bool ok = false;
    if (EVP_PKEY_verify_init(ctx) == 1 &&
        EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) == 1) {
        const std::vector<uint8_t> der_sig = ecdsa_rs_to_der(signature);
        ok = (EVP_PKEY_verify(ctx,
                              der_sig.data(),
                              der_sig.size(),
                              digest.data(),
                              digest.size()) == 1);
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

static bool is_legacy_p256_attestation_material(const std::vector<uint8_t>& pubkey,
                                                const std::vector<uint8_t>& signature) {
    return pubkey.size() == 65 && !pubkey.empty() && pubkey[0] == 0x04 && signature.size() == 64;
}

static bool verify_tee_attestation_signature_local(const std::vector<uint8_t>& pubkey,
                                                   const std::vector<uint8_t>& signed_data,
                                                   const std::vector<uint8_t>& signature,
                                                   bool allow_legacy_p256 = true) {
    if (pubkey.empty() || signature.empty() || signed_data.empty()) {
        return false;
    }

    if (is_legacy_p256_attestation_material(pubkey, signature)) {
        if (!allow_legacy_p256) {
            return false;
        }
        return verify_p256_attestation_signature_local(pubkey, signed_data, signature);
    }

    try {
        return Dilithium::verify(pubkey, signed_data, signature);
    } catch (const std::invalid_argument&) {
        return false;
    }
}

static void update_sha3_512_with_file(EVP_MD_CTX* ctx, const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw SecurityViolation("Impossible d'ouvrir le firmware/binaire: " + path.string());
    }

    std::array<unsigned char, 1 << 15> chunk{};
    while (f.good()) {
        f.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = f.gcount();
        if (got > 0) {
            if (EVP_DigestUpdate(ctx, chunk.data(), static_cast<size_t>(got)) != 1) {
                throw SecurityViolation("EVP_DigestUpdate failed");
            }
        }
    }
}

static std::vector<uint8_t> sha3_512_file(const std::filesystem::path& path) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw SecurityViolation("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw SecurityViolation("EVP_DigestInit_ex(SHA3-512) failed");
    }
    update_sha3_512_with_file(ctx, path);
    std::vector<uint8_t> out(64);
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != 64) {
        EVP_MD_CTX_free(ctx);
        throw SecurityViolation("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

struct MeasuredBootEvidence {
    std::vector<uint8_t> payload;
    std::vector<uint8_t> digest;
};

struct AssetManifestEvidence {
    std::vector<uint8_t> payload;
    std::vector<uint8_t> digest;
    std::uint64_t version = 0;
    std::string slot;
};

static bool ends_with_copy(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::filesystem::path normalize_path_for_compare(const std::filesystem::path& path) {
    try {
        return std::filesystem::weakly_canonical(path);
    } catch (...) {
        return std::filesystem::absolute(path);
    }
}

static std::filesystem::path resolve_manifest_asset_path(const std::filesystem::path& manifest_dir,
                                                         const std::string& value) {
    const std::filesystem::path raw(value);
    if (raw.is_absolute()) {
        return normalize_path_for_compare(raw);
    }
    return normalize_path_for_compare(manifest_dir / raw);
}

struct RpmbDetectionResult {
    bool available = false;
    std::string reason;
};

static RpmbDetectionResult detect_rpmb_rollback_storage()
{
#ifdef _WIN32
    return {false, "RPMB unavailable on Windows"};
#else
    auto read_small_text_file = [](const std::filesystem::path& path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) {
            return {};
        }
        std::string value;
        std::getline(f, value);
        while (!value.empty() &&
               (value.back() == '\n' || value.back() == '\r' ||
                value.back() == ' ' || value.back() == '\t')) {
            value.pop_back();
        }
        return value;
    };

    try {
        const std::filesystem::path dev_dir("/dev");
        if (std::filesystem::exists(dev_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(dev_dir)) {
                if (!entry.is_character_file() && !entry.is_block_file()) {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (name.size() >= 4 && name.find("rpmb") != std::string::npos) {
                    return {true, "RPMB device node visible at " + entry.path().string()};
                }
            }
        }

        const std::filesystem::path mmc_rpmb_devices("/sys/bus/mmc_rpmb/devices");
        if (std::filesystem::exists(mmc_rpmb_devices)) {
            for (const auto& entry : std::filesystem::directory_iterator(mmc_rpmb_devices)) {
                (void)entry;
                return {true, "RPMB exposed via /sys/bus/mmc_rpmb/devices"};
            }
        }

        const std::filesystem::path mmc_devices("/sys/bus/mmc/devices");
        if (std::filesystem::exists(mmc_devices)) {
            bool saw_sd_card = false;
            bool saw_mmc_without_rpmb = false;
            for (const auto& entry : std::filesystem::directory_iterator(mmc_devices)) {
                const std::filesystem::path base = entry.path();
                const std::string type = read_small_text_file(base / "type");
                const std::string name = read_small_text_file(base / "name");
                const std::string raw_rpmb_mult = read_small_text_file(base / "raw_rpmb_size_mult");
                if (!raw_rpmb_mult.empty()) {
                    try {
                        if (std::stoul(raw_rpmb_mult) > 0) {
                            return {true, "MMC device " + (name.empty() ? base.filename().string() : name) +
                                              " advertises raw_rpmb_size_mult=" + raw_rpmb_mult};
                        }
                    } catch (...) {
                    }
                }
                if (type == "SD") {
                    saw_sd_card = true;
                } else if (type == "MMC") {
                    saw_mmc_without_rpmb = true;
                }
            }
            if (saw_sd_card && !saw_mmc_without_rpmb) {
                return {false, "boot media is SD and exposes no RPMB capability"};
            }
            if (saw_mmc_without_rpmb) {
                return {false, "MMC media detected but no RPMB capability was exposed by sysfs"};
            }
        }
        return {false, "no RPMB capability exposed by /dev, /sys/bus/mmc_rpmb, or MMC sysfs attributes"};
    } catch (const std::exception& e) {
        return {false, std::string("RPMB detection failed: ") + e.what()};
    } catch (...) {
        return {false, "RPMB detection failed"};
    }
#endif
}

static MeasuredBootEvidence load_measured_boot_evidence_or_throw(const SecurityPolicy& policy) {
    MeasuredBootEvidence evidence;
    evidence.digest.assign(64, 0);

    if (!policy.require_boot_measure) {
        return evidence;
    }
    if (policy.boot_measure_path.empty() ||
        policy.boot_measure_sig_path.empty() ||
        policy.boot_measure_pubkey_path.empty()) {
        throw SecurityViolation("Boot measure required but paths not configured");
    }

    evidence.payload = read_file_binary(policy.boot_measure_path);
    std::vector<uint8_t> sig_raw = read_file_binary(policy.boot_measure_sig_path);
    std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;
    if (!verify_ed25519_signature(evidence.payload, sig, policy.boot_measure_pubkey_path)) {
        throw SecurityViolation("Boot measure signature invalid");
    }

    const auto kv = parse_kv_manifest_or_throw(evidence.payload);
    auto get_required = [&kv](const std::string& key) -> std::string {
        auto it = kv.find(key);
        if (it == kv.end() || it->second.empty()) {
            throw SecurityViolation("Measured boot field missing: " + key);
        }
        return it->second;
    };

    if (!policy.boot_measure_format.empty()) {
        const std::string format = get_required("format");
        if (format != policy.boot_measure_format) {
            throw SecurityViolation("Measured boot format mismatch");
        }
    }

    if (!parse_bool_like(get_required("secure_boot"))) {
        throw SecurityViolation("Measured boot indicates secure boot disabled");
    }

    if (!policy.boot_measure_expected_source.empty()) {
        const std::string source = get_required("source");
        if (source != policy.boot_measure_expected_source) {
            throw SecurityViolation("Measured boot source mismatch");
        }
    }

    const std::filesystem::path self_path = get_self_path();
    std::vector<uint8_t> expected_binary_hash = sha3_512_file(self_path);

    std::string manifest_hash_hex;
    for (const char* key : {"binary_sha3_512", "firmware_sha3_512", "image_sha3_512"}) {
        auto it = kv.find(key);
        if (it != kv.end() && !it->second.empty()) {
            manifest_hash_hex = it->second;
            break;
        }
    }
    if (manifest_hash_hex.empty()) {
        throw SecurityViolation("Measured boot manifest missing binary SHA3-512");
    }
    if (hex_to_bytes_or_throw(manifest_hash_hex) != expected_binary_hash) {
        throw SecurityViolation("Measured boot manifest does not match running binary");
    }

    const std::uint64_t expected_fw_version = read_firmware_version(policy);
    if (expected_fw_version != 0) {
        const std::string fw_value = get_required("firmware_version");
        std::uint64_t manifest_fw_version = 0;
        try {
            manifest_fw_version = static_cast<std::uint64_t>(std::stoull(fw_value));
        } catch (...) {
            throw SecurityViolation("Measured boot firmware_version invalid");
        }
        if (manifest_fw_version != expected_fw_version) {
            throw SecurityViolation("Measured boot firmware_version mismatch");
        }
    }

    if (policy.boot_measure_max_age_sec != 0) {
        std::uint64_t manifest_ts = 0;
        if (auto it = kv.find("timestamp_unix"); it != kv.end() && !it->second.empty()) {
            try {
                manifest_ts = static_cast<std::uint64_t>(std::stoull(it->second));
            } catch (...) {
                throw SecurityViolation("Measured boot timestamp_unix invalid");
            }
        } else if (auto it_ms = kv.find("timestamp_ms"); it_ms != kv.end() && !it_ms->second.empty()) {
            try {
                manifest_ts = static_cast<std::uint64_t>(std::stoull(it_ms->second) / 1000ULL);
            } catch (...) {
                throw SecurityViolation("Measured boot timestamp_ms invalid");
            }
        } else {
            throw SecurityViolation("Measured boot freshness required but no timestamp present");
        }

        const std::uint64_t now_sec = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        if (manifest_ts > now_sec + 60ULL) {
            throw SecurityViolation("Measured boot timestamp is in the future");
        }
        if (now_sec > manifest_ts && (now_sec - manifest_ts) > policy.boot_measure_max_age_sec) {
            throw SecurityViolation("Measured boot evidence too old");
        }
    }

    evidence.digest = hash_data(evidence.payload);
    return evidence;
}

static AssetManifestEvidence load_asset_manifest_evidence_or_throw(const SecurityPolicy& policy) {
    AssetManifestEvidence evidence;
    evidence.digest.assign(64, 0);

    if (!policy.require_asset_manifest) {
        return evidence;
    }
    if (policy.asset_manifest_path.empty() ||
        policy.asset_manifest_sig_path.empty() ||
        policy.asset_manifest_pubkey_path.empty()) {
        throw SecurityViolation("Asset manifest required but paths not configured");
    }

    const std::filesystem::path manifest_path(policy.asset_manifest_path);
    const std::filesystem::path manifest_dir = manifest_path.parent_path();
    evidence.payload = read_file_binary(manifest_path);
    std::vector<uint8_t> sig_raw = read_file_binary(policy.asset_manifest_sig_path);
    std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;
    if (!verify_ed25519_signature(evidence.payload, sig, policy.asset_manifest_pubkey_path)) {
        throw SecurityViolation("Asset manifest signature invalid");
    }

    const auto kv = parse_kv_manifest_or_throw(evidence.payload);
    auto get_required = [&kv](const std::string& key) -> std::string {
        auto it = kv.find(key);
        if (it == kv.end() || it->second.empty()) {
            throw SecurityViolation("Asset manifest field missing: " + key);
        }
        return it->second;
    };

    if (!policy.asset_manifest_format.empty()) {
        const std::string format = get_required("format");
        if (format != policy.asset_manifest_format) {
            throw SecurityViolation("Asset manifest format mismatch");
        }
    }

    const std::string version_text = get_required("version");
    try {
        evidence.version = static_cast<std::uint64_t>(std::stoull(version_text));
    } catch (...) {
        throw SecurityViolation("Asset manifest version invalid");
    }

    if (!policy.asset_manifest_expected_slot.empty()) {
        const std::string slot = get_required("slot");
        if (slot != policy.asset_manifest_expected_slot) {
            throw SecurityViolation("Asset manifest slot mismatch");
        }
    }
    if (auto it = kv.find("slot"); it != kv.end()) {
        evidence.slot = it->second;
    }

    if (policy.asset_manifest_max_age_sec != 0) {
        std::uint64_t manifest_ts = 0;
        if (auto it = kv.find("timestamp_unix"); it != kv.end() && !it->second.empty()) {
            try {
                manifest_ts = static_cast<std::uint64_t>(std::stoull(it->second));
            } catch (...) {
                throw SecurityViolation("Asset manifest timestamp_unix invalid");
            }
        } else if (auto it_ms = kv.find("timestamp_ms"); it_ms != kv.end() && !it_ms->second.empty()) {
            try {
                manifest_ts = static_cast<std::uint64_t>(std::stoull(it_ms->second) / 1000ULL);
            } catch (...) {
                throw SecurityViolation("Asset manifest timestamp_ms invalid");
            }
        } else {
            throw SecurityViolation("Asset manifest freshness required but no timestamp present");
        }

        const std::uint64_t now_sec = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        if (manifest_ts > now_sec + 60ULL) {
            throw SecurityViolation("Asset manifest timestamp is in the future");
        }
        if (now_sec > manifest_ts && (now_sec - manifest_ts) > policy.asset_manifest_max_age_sec) {
            throw SecurityViolation("Asset manifest evidence too old");
        }
    }

    const std::vector<std::pair<std::string, std::filesystem::path>> required_assets = {
        {"asset.yolo_engine", normalize_path_for_compare(Config::YOLO_ENGINE)},
        {"asset.midas_engine", normalize_path_for_compare(Config::MIDAS_ENGINE)},
        {"asset.policy", normalize_path_for_compare(policy.policy_path)}
    };

    for (const auto& [prefix, expected_path] : required_assets) {
        const std::string manifest_asset_path = get_required(prefix + ".path");
        const std::string manifest_asset_hash = get_required(prefix + ".sha3_512");
        const std::filesystem::path resolved_path = resolve_manifest_asset_path(manifest_dir, manifest_asset_path);
        if (resolved_path != expected_path) {
            throw SecurityViolation("Asset manifest path mismatch for " + prefix);
        }
        if (!std::filesystem::exists(resolved_path)) {
            throw SecurityViolation("Asset manifest target missing for " + prefix);
        }
        if (hex_to_bytes_or_throw(manifest_asset_hash) != sha3_512_file(resolved_path)) {
            throw SecurityViolation("Asset manifest digest mismatch for " + prefix);
        }
    }

    for (const auto& entry : kv) {
        if (!ends_with_copy(entry.first, ".path")) {
            continue;
        }

        const std::string prefix = entry.first.substr(0, entry.first.size() - 5);
        const std::string required_key = prefix + ".required";
        const std::string digest_key = prefix + ".sha3_512";
        const bool required = (kv.find(required_key) == kv.end()) ? true : parse_bool_like(kv.at(required_key));
        auto digest_it = kv.find(digest_key);
        if (digest_it == kv.end() || digest_it->second.empty()) {
            if (required) {
                throw SecurityViolation("Asset manifest digest missing for " + prefix);
            }
            continue;
        }

        const std::filesystem::path resolved_path = resolve_manifest_asset_path(manifest_dir, entry.second);
        if (!std::filesystem::exists(resolved_path)) {
            if (required) {
                throw SecurityViolation("Asset manifest target missing for " + prefix);
            }
            continue;
        }
        if (hex_to_bytes_or_throw(digest_it->second) != sha3_512_file(resolved_path)) {
            throw SecurityViolation("Asset manifest digest mismatch for " + prefix);
        }
    }

    evidence.digest = hash_data(evidence.payload);
    return evidence;
}

} // namespace

std::vector<uint8_t> HesiaDrone::derive_video_key(const std::vector<uint8_t>& session_key) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
    }
    
    if (EVP_PKEY_derive_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_derive_init failed");
    }
    
    if (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha3_512()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_CTX_set_hkdf_md failed");
    }
    
    if (EVP_PKEY_CTX_set1_hkdf_key(ctx, session_key.data(), session_key.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_CTX_set1_hkdf_key failed");
    }
    
    std::string info = "HESIA_VIDEO_STREAM_v1";
    if (EVP_PKEY_CTX_add1_hkdf_info(ctx, reinterpret_cast<const unsigned char*>(info.data()), info.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_CTX_add1_hkdf_info failed");
    }
    
    std::vector<uint8_t> video_key(32); // 32 bytes pour AES-256
    size_t outlen = 32;
    if (EVP_PKEY_derive(ctx, video_key.data(), &outlen) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_derive failed");
    }
    
    EVP_PKEY_CTX_free(ctx);
    return video_key;
}

std::vector<uint8_t> HesiaDrone::compute_firmware_sha3_512() {
    // Hash du binaire courant + mesures boot (optionnel) en SHA3-512.
    const std::filesystem::path self_path = get_self_path();

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex(SHA3-512) failed");
    }

    try {
        update_sha3_512_with_file(ctx, self_path);
        MeasuredBootEvidence evidence = load_measured_boot_evidence_or_throw(policy_);
        if (!evidence.payload.empty()) {
            if (EVP_DigestUpdate(ctx, evidence.payload.data(), evidence.payload.size()) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate boot measure failed");
            }
        }
        AssetManifestEvidence asset_manifest = load_asset_manifest_evidence_or_throw(policy_);
        if (!asset_manifest.payload.empty()) {
            if (EVP_DigestUpdate(ctx, asset_manifest.payload.data(), asset_manifest.payload.size()) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate asset manifest failed");
            }
        }
    } catch (...) {
        EVP_MD_CTX_free(ctx);
        throw;
    }

    std::vector<uint8_t> out(64);
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != 64) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> HesiaDrone::sign_with_drone_identity(const std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        throw SecurityViolation("Drone signing payload is empty");
    }

    if (tee_anchored_dilithium_ && optee_mldsa_signing_ready()) {
        return optee_sign_mldsa_payload(payload);
    }

    if (tee_anchored_dilithium_) {
        if (sealed_dilithium_path_.empty()) {
            throw SecurityViolation("TEE-anchored Dilithium path not configured");
        }

        std::vector<uint8_t> blob = optee_unseal_file(sealed_dilithium_path_, 0);
        auto keys = parse_dilithium_keys(blob);
        if (!ConstantTime::equals(keys.first, drone_keypair.first)) {
            wipe_sensitive_vector(keys.second);
            wipe_sensitive_vector(keys.first);
            wipe_sensitive_vector(blob);
            throw SecurityViolation("TEE-anchored Dilithium public key mismatch");
        }

        std::vector<uint8_t> signature = Dilithium::sign(keys.second, payload);
        wipe_sensitive_vector(keys.second);
        wipe_sensitive_vector(keys.first);
        wipe_sensitive_vector(blob);
        return signature;
    }

    if (drone_keypair.second.empty()) {
        throw SecurityViolation("Drone private signing key unavailable");
    }
    return Dilithium::sign(drone_keypair.second, payload);
}

static void append_u32_be(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void append_len_prefixed(std::vector<uint8_t>& out, const std::vector<uint8_t>& value)
{
    append_u32_be(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static void append_len_prefixed(std::vector<uint8_t>& out, const std::string& value)
{
    append_u32_be(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static std::vector<uint8_t> build_signed_drone_auth_payload(const BlockDroneAuth& auth)
{
    std::vector<uint8_t> payload;
    payload.reserve(auth.drone_id.size() + auth.drone_pubkey.size() + auth.firmware_hash.size() +
                    auth.puf_response.size() + auth.last_block_hash.size() + auth.session_id.size() +
                    auth.transcript_hash.size() + auth.server_cert_sha256.size() +
                    auth.boot_measure_digest.size() + auth.tee_attestation_pubkey.size() + 10 * 4);
    append_len_prefixed(payload, auth.drone_id);
    append_len_prefixed(payload, auth.drone_pubkey);
    append_len_prefixed(payload, auth.firmware_hash);
    append_len_prefixed(payload, auth.puf_response);
    append_len_prefixed(payload, auth.last_block_hash);
    append_len_prefixed(payload, auth.session_id);
    append_len_prefixed(payload, auth.transcript_hash);
    append_len_prefixed(payload, auth.server_cert_sha256);
    append_len_prefixed(payload, auth.boot_measure_digest);
    append_len_prefixed(payload, auth.tee_attestation_pubkey);
    return payload;
}

static uint32_t read_u32_be(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static std::vector<uint8_t> serialize_dilithium_keys(const std::vector<uint8_t>& pk,
                                                     const std::vector<uint8_t>& sk)
{
    std::vector<uint8_t> blob;
    blob.reserve(12 + pk.size() + sk.size());
    blob.push_back('H');
    blob.push_back('D');
    blob.push_back('K');
    blob.push_back('1');
    append_u32_be(blob, static_cast<uint32_t>(pk.size()));
    append_u32_be(blob, static_cast<uint32_t>(sk.size()));
    blob.insert(blob.end(), pk.begin(), pk.end());
    blob.insert(blob.end(), sk.begin(), sk.end());
    return blob;
}

static std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
parse_dilithium_keys(const std::vector<uint8_t>& blob)
{
    if (blob.size() < 12) {
        throw SecurityViolation("Dilithium key blob too small");
    }
    if (!(blob[0] == 'H' && blob[1] == 'D' && blob[2] == 'K' && blob[3] == '1')) {
        throw SecurityViolation("Dilithium key blob magic mismatch");
    }
    uint32_t pk_len = read_u32_be(blob.data() + 4);
    uint32_t sk_len = read_u32_be(blob.data() + 8);
    size_t expected = 12u + pk_len + sk_len;
    if (blob.size() != expected) {
        throw SecurityViolation("Dilithium key blob size mismatch");
    }
    std::vector<uint8_t> pk(blob.begin() + 12, blob.begin() + 12 + pk_len);
    std::vector<uint8_t> sk(blob.begin() + 12 + pk_len, blob.end());
    return {pk, sk};
}

static void wipe_sensitive_vector(std::vector<uint8_t>& data)
{
    if (!data.empty()) {
        SecureMemory::zeroize(data);
    }
}

static void write_public_key(const std::filesystem::path& path, const std::vector<uint8_t>& pk)
{
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw SecurityViolation("Cannot write Dilithium public key: " + path.string());
    }
    if (!pk.empty()) {
        f.write(reinterpret_cast<const char*>(pk.data()), static_cast<std::streamsize>(pk.size()));
    }
}

static std::vector<uint8_t> load_pinned_server_pubkey_or_throw(const SecurityPolicy& policy)
{
    const std::filesystem::path secure_dir(policy.secure_dir);
    const std::vector<std::filesystem::path> candidates = {
        secure_dir / "server_public.bin",
        secure_dir / "server_mldsa87_public.bin",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return read_file_binary(candidate);
        }
    }

    throw SecurityViolation("Pinned server ML-DSA public key missing in secure_dir");
}


HesiaDrone::HesiaDrone(const std::string& did) 
    : state(DroneState::IDLE), drone_id(did), seq(0) {
    last_block_hash.assign(64, 0);

    policy_ = load_security_policy_or_throw("drone");
    const std::filesystem::path secure_dir(policy_.secure_dir);
    const std::filesystem::path optee_session_auth_path = policy_.optee_session_auth_path.empty()
        ? (secure_dir / "optee_session_auth.sealed")
        : std::filesystem::path(resolve_path(policy_.secure_dir, policy_.optee_session_auth_path));
    optee_set_session_auth_secret_path(optee_session_auth_path);
    optee_require_session_auth(policy_.prod_fuse || policy_.require_optee_session_auth);
    optee_require_session_auth_ready_or_throw();

    if (policy_.require_release_signature) {
        std::filesystem::path target = policy_.release_target_path.empty()
            ? get_self_path()
            : std::filesystem::path(policy_.release_target_path);
        std::filesystem::path sig_path = policy_.release_sig_path;
        std::filesystem::path pub_path = policy_.release_pubkey_path;
        if (target.empty() || sig_path.empty() || pub_path.empty()) {
            throw SecurityViolation("Release signature required but paths not configured");
        }
        std::vector<uint8_t> data = read_file_binary(target);
        std::vector<uint8_t> sig_raw = read_file_binary(sig_path);
        std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;
        if (!verify_ed25519_signature(data, sig, pub_path)) {
            throw SecurityViolation("Release signature verification failed");
        }
    }
    
    // ✅ Utiliser la clé publique du serveur intégrée pour vérifier les signatures du serveur
    try {
        server_pubkey = load_pinned_server_pubkey_or_throw(policy_);
    } catch (const std::exception&) {
        if (policy_.prod_fuse || !env_flag_enabled("HESIA_ALLOW_DEMO_SERVER_KEYS")) {
            throw;
        }
        server_pubkey.resize(demo_public_bin_len);
        std::copy(demo_public_bin, demo_public_bin + demo_public_bin_len, server_pubkey.begin());
    }
    
    // Charger la clé privée Dilithium (ancrée via OP-TEE) ou générer/sceller si absente
    const bool allow_ephemeral_dilithium = policy_.allow_ephemeral_dilithium;
    sealed_dilithium_path_ = policy_.sealed_dilithium_path.empty()
        ? (secure_dir / "dilithium5_sk.sealed")
        : std::filesystem::path(resolve_path(policy_.secure_dir, policy_.sealed_dilithium_path));

    bool key_loaded = false;
    if (optee_available()) {
        std::filesystem::path sealed_path = sealed_dilithium_path_;
        std::filesystem::path public_path = sealed_path.parent_path() / "dilithium5_pk.bin";
        const bool sealed_exists = std::filesystem::exists(sealed_path);
        try {
            if (optee_mldsa_signing_ready()) {
                tee_anchored_dilithium_ = true;
                drone_keypair.first = optee_get_mldsa_public_key();
                drone_keypair.second.clear();
                try {
                    write_public_key(public_path, drone_keypair.first);
                } catch (...) {
                    // Best-effort export for provisioning consistency.
                }
                key_loaded = !drone_keypair.first.empty();
            }

            if (!key_loaded && sealed_exists) {
                try {
                    if (optee_import_mldsa_key_from_sealed_blob(sealed_path)) {
                        tee_anchored_dilithium_ = true;
                        try {
                            drone_keypair.first = optee_get_mldsa_public_key();
                        } catch (...) {
                            if (std::filesystem::exists(public_path)) {
                                drone_keypair.first = read_file_binary(public_path);
                            } else {
                                throw;
                            }
                        }
                        drone_keypair.second.clear();
                        key_loaded = !drone_keypair.first.empty();
                    }
                } catch (...) {
                    if (policy_.prod_fuse && policy_.require_mldsa_sign_in_tee) {
                        throw;
                    }
                }
            }

            if (!key_loaded && sealed_exists &&
                !(policy_.prod_fuse && policy_.require_mldsa_sign_in_tee)) {
                std::vector<uint8_t> blob = optee_unseal_file(sealed_path, 0);
                auto keys = parse_dilithium_keys(blob);
                drone_keypair.first = keys.first;
                drone_keypair.second.clear();
                tee_anchored_dilithium_ = true;
                try {
                    write_public_key(public_path, drone_keypair.first);
                } catch (...) {
                    // Best-effort export for provisioning consistency.
                }
                wipe_sensitive_vector(keys.second);
                wipe_sensitive_vector(keys.first);
                wipe_sensitive_vector(blob);
                key_loaded = true;
            }
        } catch (const std::exception& e) {
            if (sealed_exists && !allow_ephemeral_dilithium) {
                throw SecurityViolation(std::string("Dilithium sealed key invalid: ") + e.what());
            }
            key_loaded = false;
        }
    }

    if (!key_loaded) {
        if (!optee_available() && !allow_ephemeral_dilithium) {
            throw SecurityViolation("OP-TEE unavailable for Dilithium private key anchoring");
        }
        if (policy_.prod_fuse && policy_.require_mldsa_sign_in_tee) {
            throw SecurityViolation("Production ML-DSA in-TEE signing requires a provisioned sealed key; refusing REE key generation");
        }

        auto kp = Dilithium::generate_keypair();
        if (optee_available()) {
            std::filesystem::path sealed_path = sealed_dilithium_path_;
            std::filesystem::path public_path = sealed_path.parent_path() / "dilithium5_pk.bin";
            std::vector<uint8_t> blob = serialize_dilithium_keys(kp.first, kp.second);
            optee_seal_file(sealed_path, blob);
            drone_keypair.first = kp.first;
            drone_keypair.second.clear();
            tee_anchored_dilithium_ = true;
            try {
                write_public_key(public_path, kp.first);
            } catch (...) {
                // Best-effort only: public key can be exported separately if needed.
            }
            try {
                (void)optee_import_mldsa_key_from_sealed_blob(sealed_path);
            } catch (...) {
                if (policy_.prod_fuse && policy_.require_mldsa_sign_in_tee) {
                    throw;
                }
            }
            wipe_sensitive_vector(blob);
            wipe_sensitive_vector(kp.second);
            wipe_sensitive_vector(kp.first);
        } else {
            drone_keypair = std::move(kp);
            (void)SecureMemory::protect(drone_keypair.second);
        }
    }

    if (policy_.prod_fuse && policy_.require_mldsa_sign_in_tee && !optee_mldsa_signing_ready()) {
        throw SecurityViolation("Production policy requires ML-DSA signing fully inside OP-TEE, but this build does not provide it");
    }

    if (policy_.require_tee_attestation) {
        if (!optee_available()) {
            throw SecurityViolation("OP-TEE required for TEE attestation signing");
        }
        if (!optee_mldsa_signing_ready()) {
            throw SecurityViolation("TEE attestation now requires ML-DSA signing inside OP-TEE");
        }
        const std::filesystem::path tee_pubkey_path = secure_dir / "drone_tee_attest_pub.bin";
        const std::vector<uint8_t> tee_pubkey = optee_get_mldsa_public_key();
        if (tee_pubkey.empty()) {
            throw SecurityViolation("Invalid TEE attestation public key size");
        }
        if (!drone_keypair.first.empty() &&
            !ConstantTime::equals(tee_pubkey, drone_keypair.first)) {
            throw SecurityViolation("TEE attestation public key does not match provisioned ML-DSA identity key");
        }
        const std::vector<uint8_t> self_test_msg = {
            'H','E','S','I','A','-','T','E','E','-','A','T','T','E','S','T',
            '-','S','E','L','F','T','E','S','T','-','v','2'
        };
        const std::vector<uint8_t> self_test_digest = sha256_bytes(self_test_msg);
        const std::vector<uint8_t> self_test_signature =
            optee_sign_mldsa_payload(self_test_digest);
        if (!verify_tee_attestation_signature_local(tee_pubkey,
                                                    self_test_digest,
                                                    self_test_signature,
                                                    false)) {
            throw SecurityViolation("Provisioned TEE attestation public key does not match TA signing key");
        }
        try {
            write_public_key(tee_pubkey_path, tee_pubkey);
        } catch (...) {
            // Best-effort export for provisioning; the runtime attestation path already has the key in TEE.
        }
    }

    // Load PUF secret from OP-TEE sealed storage (fail-closed by default).
    const bool allow_ephemeral = policy_.allow_ephemeral_puf;

    try {
        std::filesystem::path sealed_path = policy_.sealed_puf_path.empty()
            ? (secure_dir / "hesia_seed.sealed")
            : std::filesystem::path(resolve_path(policy_.secure_dir, policy_.sealed_puf_path));
        puf_secret = optee_unseal_file(sealed_path, 32);
    } catch (const std::exception& e) {
        if (!allow_ephemeral) {
            throw SecurityViolation(std::string("PUF unseal failed: ") + e.what());
        }
        puf_secret.resize(32);
        if (RAND_bytes(puf_secret.data(), 32) != 1) {
            throw std::runtime_error("RAND_bytes failed");
        }
    }

    // OEM fuse KDF (SP800-108) - required if policy enforces it.
    {
        std::vector<uint8_t> oem_key = derive_oem_kdf_key(policy_);
        if (oem_key.empty()) {
            if (policy_.require_oem_kdf) {
                throw SecurityViolation("OEM KDF required but K1/K2 missing or invalid");
            }
        } else {
            std::vector<uint8_t> mix;
            mix.reserve(oem_key.size() + puf_secret.size());
            mix.insert(mix.end(), oem_key.begin(), oem_key.end());
            mix.insert(mix.end(), puf_secret.begin(), puf_secret.end());
            std::vector<uint8_t> combined = hash_data(mix);
            puf_secret.assign(combined.begin(), combined.begin() + 32);
        }
    }

    // Anti-rollback: check monotonic firmware version in TEE
    if (policy_.prod_fuse) {
        const std::uint64_t fw_version = read_firmware_version(policy_);
        const AssetManifestEvidence asset_manifest = load_asset_manifest_evidence_or_throw(policy_);
        if (fw_version == 0) {
            throw SecurityViolation("Firmware version missing for anti-rollback");
        }
        if (policy_.require_asset_manifest && asset_manifest.version != 0 && asset_manifest.version != fw_version) {
            throw SecurityViolation("Asset manifest version mismatch with firmware version");
        }
        if (!optee_available()) {
            throw SecurityViolation("OP-TEE required for anti-rollback protection");
        }
        if (policy_.require_rpmb_rollback_storage) {
            const RpmbDetectionResult rpmb = detect_rpmb_rollback_storage();
            if (!rpmb.available) {
                throw SecurityViolation("RPMB-backed rollback storage required but unavailable: " + rpmb.reason);
            }
        }
        if (policy_.require_ab_slots) {
            if (asset_manifest.slot.empty()) {
                throw SecurityViolation("A/B slot protection requires slot field in asset manifest");
            }
            const OpteeSlotId slot_id = optee_slot_id_from_string(asset_manifest.slot);
            if (slot_id == OpteeSlotId::Invalid) {
                throw SecurityViolation("Invalid slot value in asset manifest");
            }
            if (!optee_commit_slot_boot(slot_id, fw_version, asset_manifest.version)) {
                throw SecurityViolation("A/B slot boot commit rejected by OP-TEE");
            }
        } else if (!optee_check_and_update_firmware_version(fw_version)) {
            throw SecurityViolation("Firmware rollback detected");
        }
    }
}

HesiaDrone::~HesiaDrone() {
    SecureMemory::zeroize(last_block_hash);
    SecureMemory::zeroize(session_key);
    SecureMemory::zeroize(session_id);
    SecureMemory::zeroize(key_exchange_transcript_hash);
    SecureMemory::zeroize(tls_exporter_secret);
    SecureMemory::zeroize(tls_peer_cert_sha256);
    SecureMemory::zeroize(drone_keypair.first);
    SecureMemory::zeroize(drone_keypair.second);
    SecureMemory::zeroize(puf_secret);
    SecureMemory::zeroize(hello_random);
    SecureMemory::zeroize(server_pubkey);
    SecureMemory::zeroize(encrypted_msg);
}

Hello HesiaDrone::build_hello() {
    if (state != DroneState::IDLE) {
        throw InvalidStateError(drone_state_to_string(state), "IDLE");
    }
    
    hello_random.resize(64);
    if (RAND_bytes(hello_random.data(), 64) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    
    state = DroneState::HELLO_SENT;
    
    Hello hello;
    hello.random_64 = hello_random;
    hello.proto_version = 1;
    // Feature bitmask:
    // 0x01 = basic HESIA v1
    // 0x02 = client supports TLS exporter binding (32B) for hybrid key schedule
    // 0x04 = client requires TLS exporter binding (fail-closed if server cannot)
    uint32_t features = 0x01;
    // Advertise exporter support only if we have a valid 32B exporter.
    if (tls_exporter_secret.size() == 32) {
        features |= 0x02;
        if (policy_.require_exporter_binding) {
            features |= 0x04;
        }
    }
    hello.features = features;
    return hello;
}

void HesiaDrone::handle_hello_ack(const HelloAck& ack) {
    if (state != DroneState::HELLO_SENT) {
        throw InvalidStateError(drone_state_to_string(state), "HELLO_SENT");
    }
    
    std::vector<uint8_t> salt = {0x44, 0x52, 0x4F, 0x4E, 0x45, 0x5F, 0x53, 0x41, 0x4C, 0x54}; // "DRONE_SALT"
    std::vector<uint8_t> input;
    input.reserve(hello_random.size() + salt.size());
    input.insert(input.end(), hello_random.begin(), hello_random.end());
    input.insert(input.end(), salt.begin(), salt.end());
    
    std::vector<uint8_t> expected = hash_data(input);
    
    if (!ConstantTime::equals(ack.response_hash, expected)) {
        throw AuthenticationFailed("HELLO_ACK invalide");
    }

    // Store server capabilities and determine whether TLS exporter binding is negotiated.
    // Capability bitmask:
    // 0x02 = use TLS exporter binding (only if both sides support it)
    server_capabilities = ack.capabilities;
    use_tls_exporter_binding = ((ack.capabilities & 0x02u) != 0u);
    if (!use_tls_exporter_binding) {
        throw SecurityViolation("Le serveur n'a pas annonce le binding TLS exporter obligatoire");
    }

    // Fail closed if exporter binding is negotiated but the local transport layer
    // could not provide a valid exporter. This prevents later silent mismatches.
    if (use_tls_exporter_binding) {
        if (tls_exporter_secret.size() != 32) {
            throw SecurityViolation("TLS exporter binding négocié mais exporter absent/invalide côté drone");
        }
    }
    
    state = DroneState::KEY_EXCHANGE;
}

uint64_t get_current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

KeyResp HesiaDrone::handle_key_init(const KeyInit& init) {
    if (state != DroneState::KEY_EXCHANGE) {
        throw InvalidStateError(drone_state_to_string(state), "KEY_EXCHANGE");
    }

    // VÉRIFICATIONS ANTI-REJEU - Scénario 1
    const uint64_t current_time = get_current_timestamp_ms();

    // 1) Vérifier timestamp (anti-rejeu, tolérance future)
    if (init.timestamp != 0) {
        // Tolérer un léger décalage d'horloge (30s)
        constexpr uint64_t kMaxFutureSkewMs = 30ULL * 1000ULL;
        constexpr uint64_t kMaxAgeMs = 5ULL * 60ULL * 1000ULL; // 5 minutes

        if (init.timestamp > current_time + kMaxFutureSkewMs) {
            throw SecurityViolation("KeyInit timestamp dans le futur - possible manipulation de clock");
        }
        if (current_time >= init.timestamp && (current_time - init.timestamp) > kMaxAgeMs) {
            throw SecurityViolation("KeyInit timestamp trop ancien - possible rejeu");
        }
    }

    // 2) Vérifier la présence des champs de binding (mode sécurité maximale)
    if (init.session_id.empty() || init.context_hash.empty()) {
        throw SecurityViolation("KeyInit incomplet (session_id/context_hash manquants) - mode sécurité maximale");
    }

    // 3) Vérifier session_id unique avec TTL et anti-DoS
    static std::mutex sessions_mu;
    static std::unordered_map<std::string, uint64_t> used_sessions;
    static const uint64_t SESSION_TTL_MS = 300000; // 5 minutes
    static const size_t MAX_SESSIONS = 10000;

    auto prune_expired = [&](uint64_t now_ms) {
        for (auto it = used_sessions.begin(); it != used_sessions.end(); ) {
            if (it->second + SESSION_TTL_MS < now_ms) {
                it = used_sessions.erase(it);
            } else {
                ++it;
            }
        }
    };

    if (init.session_id.size() < 16 || init.session_id.size() > 128) {
        throw SecurityViolation("session_id taille invalide");
    }

    const std::string session_key_id(reinterpret_cast<const char*>(init.session_id.data()), init.session_id.size());

    {
        std::lock_guard<std::mutex> lock(sessions_mu);
        prune_expired(current_time);

        if (used_sessions.size() >= MAX_SESSIONS) {
            // Nouvelle tentative de purge (defense-in-depth)
            prune_expired(current_time);
            if (used_sessions.size() >= MAX_SESSIONS) {
                throw SecurityViolation("Trop de sessions actives - possible DoS");
            }
        }

        if (used_sessions.find(session_key_id) != used_sessions.end()) {
            throw SecurityViolation("Session ID déjà utilisé - possible rejeu");
        }

        used_sessions.emplace(session_key_id, current_time);
    }


    // Validation defensive (DoS / entrées malformées)
    if (init.kyber_pubkey.empty() || init.kyber_pubkey.size() > 16 * 1024) {
        throw SecurityViolation("kyber_pubkey taille invalide");
    }
    if (init.nonce_s.size() < 16 || init.nonce_s.size() > 64) {
        throw SecurityViolation("nonce_s taille invalide");
    }
    // 4) Vérifier le binding de contexte
    // NOTE: le serveur ne connaît pas le drone_id au moment de KEY_INIT, donc le contexte
    // ne doit pas dépendre de l'identité du drone. On utilise un label constant partagé.
    static const std::string ctx_label = "HESIA_CTX_v1";
    std::vector<uint8_t> context_data;
    context_data.reserve(ctx_label.size() + init.kyber_pubkey.size() + init.nonce_s.size() + init.session_id.size());
    context_data.insert(context_data.end(), ctx_label.begin(), ctx_label.end());
    context_data.insert(context_data.end(), init.kyber_pubkey.begin(), init.kyber_pubkey.end());
    context_data.insert(context_data.end(), init.nonce_s.begin(), init.nonce_s.end());
    context_data.insert(context_data.end(), init.session_id.begin(), init.session_id.end());

    const std::vector<uint8_t> expected_context = hash_data(context_data);
    if (!ConstantTime::equals(init.context_hash, expected_context)) {
        throw SecurityViolation("Context hash invalide - manipulation détectée");
    }

    // 5) Kyber encapsulation + KDF robuste
    auto [ciphertext, shared] = Kyber::encaps(init.kyber_pubkey);

    std::vector<uint8_t> nonce_d(16);
    if (RAND_bytes(nonce_d.data(), static_cast<int>(nonce_d.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    // Deriver une cle de session 32 octets (AES-256) via HKDF-SHA3-512
    {
        std::vector<uint8_t> salt;
        const size_t tls_binding_len = 32;
        salt.reserve(init.nonce_s.size() + nonce_d.size() + init.session_id.size() + tls_binding_len);
        salt.insert(salt.end(), init.nonce_s.begin(), init.nonce_s.end());
        salt.insert(salt.end(), nonce_d.begin(), nonce_d.end());
        salt.insert(salt.end(), init.session_id.begin(), init.session_id.end());

        if (!use_tls_exporter_binding || tls_exporter_secret.size() != 32) {
            throw SecurityViolation("TLS exporter binding requis pour deriver la cle de session");
        }

        // Always append 32 bytes for TLS binding.
        if (use_tls_exporter_binding) {
            if (tls_exporter_secret.size() != 32) {
                throw std::runtime_error("tls_exporter_secret taille invalide");
            }
            salt.insert(salt.end(), tls_exporter_secret.begin(), tls_exporter_secret.end());
        } else {
            salt.insert(salt.end(), 32, 0x00);
        }

        std::vector<uint8_t> out;
        if (policy_.require_tee_hkdf) {
            if (!optee_available()) {
                throw SecurityViolation("OP-TEE HKDF required by policy but unavailable");
            }
            try {
                const std::string info_str = "HESIA_SESSION_KEY_v1";
                std::vector<uint8_t> info(info_str.begin(), info_str.end());
                out = optee_hkdf_sha3_512(shared, salt, info, 32);
            } catch (const std::exception& e) {
                throw SecurityViolation(std::string("OP-TEE HKDF failed: ") + e.what());
            }
        }

        if (out.empty()) {
            EVP_PKEY_CTX* kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
            if (!kdf) {
                throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
            }

            if (EVP_PKEY_derive_init(kdf) <= 0) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_derive_init failed");
            }

            if (EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha3_512()) <= 0) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_CTX_set_hkdf_md failed");
            }

            if (EVP_PKEY_CTX_set1_hkdf_salt(kdf, salt.data(), static_cast<int>(salt.size())) <= 0) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_CTX_set1_hkdf_salt failed");
            }

            if (EVP_PKEY_CTX_set1_hkdf_key(kdf, shared.data(), static_cast<int>(shared.size())) <= 0) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_CTX_set1_hkdf_key failed");
            }

            const std::string info = "HESIA_SESSION_KEY_v1";
            if (EVP_PKEY_CTX_add1_hkdf_info(kdf,
                                           reinterpret_cast<const unsigned char*>(info.data()),
                                           static_cast<int>(info.size())) <= 0) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_CTX_add1_hkdf_info failed");
            }

            out.resize(32);
            size_t out_len = out.size();
            if (EVP_PKEY_derive(kdf, out.data(), &out_len) <= 0 || out_len != out.size()) {
                EVP_PKEY_CTX_free(kdf);
                throw std::runtime_error("EVP_PKEY_derive failed");
            }

            EVP_PKEY_CTX_free(kdf);
        }

        session_key = std::move(out);
    }

    KeyResp resp;
    resp.kyber_ciphertext = ciphertext;
    resp.nonce_d = nonce_d;
    resp.session_id = init.session_id; // Echo pour binding
    // Stocker session_id localement (utile pour KEY_CONFIRM)
    this->session_id = init.session_id;
    key_exchange_transcript_hash.clear();

    // Calculer response_hash pour binding contexte
    std::vector<uint8_t> response_data;
    response_data.reserve(resp.kyber_ciphertext.size() + resp.nonce_d.size() + resp.session_id.size() + session_key.size());
    response_data.insert(response_data.end(), resp.kyber_ciphertext.begin(), resp.kyber_ciphertext.end());
    response_data.insert(response_data.end(), resp.nonce_d.begin(), resp.nonce_d.end());
    response_data.insert(response_data.end(), resp.session_id.begin(), resp.session_id.end());
    response_data.insert(response_data.end(), session_key.begin(), session_key.end());
    resp.response_hash = hash_data(response_data);

    

    return resp;
}



void HesiaDrone::handle_key_confirm(const KeyConfirm& kc, const std::vector<uint8_t>& expected_transcript_hash) {
    if (state != DroneState::KEY_EXCHANGE) {
        throw InvalidStateError(drone_state_to_string(state), "KEY_EXCHANGE");
    }
    if (session_id.empty()) {
        throw SecurityViolation("Session ID non initialisé");
    }
    if (!ConstantTime::equals(kc.session_id, session_id)) {
        throw SecurityViolation("KEY_CONFIRM: session_id ne correspond pas");
    }
    if (kc.transcript_hash.size() != 64 || expected_transcript_hash.size() != 64) {
        throw SecurityViolation("KEY_CONFIRM: transcript_hash taille invalide");
    }
    if (!ConstantTime::equals(kc.transcript_hash, expected_transcript_hash)) {
        throw SecurityViolation("KEY_CONFIRM: transcript_hash invalide (binding cassé)");
    }

    // Construire le message signé (définition: session_id || transcript_hash || timestamp_be)
    std::vector<uint8_t> signed_payload;
    signed_payload.reserve(session_id.size() + 64 + 8);
    signed_payload.insert(signed_payload.end(), session_id.begin(), session_id.end());
    signed_payload.insert(signed_payload.end(), expected_transcript_hash.begin(), expected_transcript_hash.end());

    if (kc.timestamp == 0) {
        if (policy_.prod_fuse) {
            throw SecurityViolation("KEY_CONFIRM: timestamp missing in production mode");
        }
    } else {
        const uint64_t current_time = get_current_timestamp_ms();
        constexpr uint64_t kMaxFutureSkewMs = 30ULL * 1000ULL;
        constexpr uint64_t kMaxAgeMs = 5ULL * 60ULL * 1000ULL;
        if (kc.timestamp > current_time + kMaxFutureSkewMs) {
            throw SecurityViolation("KEY_CONFIRM: timestamp in the future");
        }
        if (current_time >= kc.timestamp && (current_time - kc.timestamp) > kMaxAgeMs) {
            throw SecurityViolation("KEY_CONFIRM: timestamp too old");
        }
    }

    auto append_u64_be = [&](uint64_t v) {
        for (int i = 7; i >= 0; --i) {
            signed_payload.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    append_u64_be(kc.timestamp);

    bool ok = false;
    try {
        ok = Dilithium::verify(server_pubkey, signed_payload, kc.signature);
    } catch (...) {
        ok = false;
    }

    // Compat: certains serveurs peuvent signer sans timestamp
    if (!ok && !policy_.prod_fuse) {
        signed_payload.resize(0);
        signed_payload.reserve(session_id.size() + 64);
        signed_payload.insert(signed_payload.end(), session_id.begin(), session_id.end());
        signed_payload.insert(signed_payload.end(), expected_transcript_hash.begin(), expected_transcript_hash.end());
        try {
            ok = Dilithium::verify(server_pubkey, signed_payload, kc.signature);
        } catch (...) {
            ok = false;
        }
    }

    if (!ok) {
        throw SecurityViolation("KEY_CONFIRM: signature serveur invalide");
    }

    // Stocker le transcript hash (utile pour la télémétrie/attestation ultérieure)
    key_exchange_transcript_hash = expected_transcript_hash;

    // Le canal sécurisé sera créé plus tard, après DRONE_AUTH/SERVER_AUTH.
    state = DroneState::KEY_CONFIRMED;
}

BlockDroneAuth HesiaDrone::build_drone_auth() {
    if (state != DroneState::KEY_CONFIRMED) {
        throw InvalidStateError(drone_state_to_string(state), "KEY_CONFIRMED");
    }

    if (session_key.empty()) {
        throw SessionNotEstablished("Session non établie");
    }
    if (session_id.size() < 16) {
        throw SecurityViolation("Session ID non initialisé");
    }
    if (key_exchange_transcript_hash.size() != 64) {
        throw SecurityViolation("Transcript hash non initialisé (KEY_CONFIRM manquant)");
    }

    // TLS PoP binding: si TLS est actif, on doit avoir un hash cert (SHA-256 DER)
    const bool tls_active = (tls_peer_cert_sha256.size() == 32);
    if (tls_active && tls_peer_cert_sha256.size() != 32) {
        throw SecurityViolation("TLS actif mais hash certificat serveur indisponible");
    }

    MeasuredBootEvidence boot_evidence = load_measured_boot_evidence_or_throw(policy_);
    std::vector<uint8_t> firmware_hash = compute_firmware_sha3_512();

    std::vector<uint8_t> puf_salt = session_id;
    std::vector<uint8_t> puf_info;
    puf_info.reserve(key_exchange_transcript_hash.size() + firmware_hash.size() +
                     boot_evidence.digest.size() + tls_peer_cert_sha256.size());
    puf_info.insert(puf_info.end(), key_exchange_transcript_hash.begin(), key_exchange_transcript_hash.end());
    puf_info.insert(puf_info.end(), firmware_hash.begin(), firmware_hash.end());
    puf_info.insert(puf_info.end(), boot_evidence.digest.begin(), boot_evidence.digest.end());
    puf_info.insert(puf_info.end(), tls_peer_cert_sha256.begin(), tls_peer_cert_sha256.end());

    std::vector<uint8_t> puf_response;
    if (policy_.require_tee_hkdf) {
        if (!optee_available()) {
            throw SecurityViolation("OP-TEE required for challenge-bound PUF response");
        }
        puf_response = optee_hkdf_sha3_512(puf_secret, puf_salt, puf_info, 64);
    } else {
        std::vector<uint8_t> puf_material;
        puf_material.reserve(puf_secret.size() + puf_salt.size() + puf_info.size());
        puf_material.insert(puf_material.end(), puf_secret.begin(), puf_secret.end());
        puf_material.insert(puf_material.end(), puf_salt.begin(), puf_salt.end());
        puf_material.insert(puf_material.end(), puf_info.begin(), puf_info.end());
        puf_response = hash_data(puf_material);
    }

    std::vector<uint8_t> tee_pubkey;
    if (policy_.require_tee_attestation) {
        if (!optee_mldsa_signing_ready()) {
            throw SecurityViolation("TEE attestation requires ML-DSA signing inside OP-TEE");
        }
        tee_pubkey = optee_get_mldsa_public_key();
        if (tee_pubkey.empty()) {
            throw SecurityViolation("TEE attestation public key invalid");
        }
    }

    BlockDroneAuth block;
    block.drone_id = drone_id;
    block.drone_pubkey = drone_keypair.first;
    block.firmware_hash = firmware_hash;
    block.puf_response = puf_response;
    block.last_block_hash = last_block_hash;
    block.boot_measure_digest = boot_evidence.digest;
    block.tee_attestation_pubkey = tee_pubkey;

    // Extensions PoP (appendées après signature par le serializer pour compatibilité)
    block.session_id = session_id;
    block.transcript_hash = key_exchange_transcript_hash;
    block.server_cert_sha256 = tls_peer_cert_sha256;

    // Chaînage: SHA3-512(payload)
    std::vector<uint8_t> payload = build_signed_drone_auth_payload(block);
    block.signature = sign_with_drone_identity(payload);
    if (policy_.require_tee_attestation) {
        block.tee_attestation_signature = optee_sign_mldsa_payload(sha256_bytes(payload));
        if (block.tee_attestation_signature.empty()) {
            throw SecurityViolation("TEE attestation signature invalid");
        }
    } else {
        block.tee_attestation_signature.clear();
    }

    last_block_hash = hash_data(payload);
    state = DroneState::DRONE_AUTH_SENT;

    return block;
}

std::vector<uint8_t> HesiaDrone::handle_server_auth(const BlockServerAuth& block) {
    if (state != DroneState::DRONE_AUTH_SENT) {
        throw InvalidStateError(drone_state_to_string(state), "DRONE_AUTH_SENT");
    }
    
    if (!ConstantTime::equals(block.server_pubkey, server_pubkey)) {
        throw AuthenticationFailed("SERVER_AUTH: pinned server public key mismatch");
    }

    const std::vector<uint8_t> local_policy_hash = compute_policy_hash_or_throw(policy_);
    if (!ConstantTime::equals(block.policy_hash, local_policy_hash)) {
        throw AuthenticationFailed("SERVER_AUTH: policy hash mismatch");
    }

    std::vector<uint8_t> payload;
    payload.reserve(block.server_id.size() + block.server_pubkey.size() + block.mission_id.size() +
                    block.policy_hash.size() + block.last_block_hash.size() + 5 * 4);
    append_len_prefixed(payload, block.server_id);
    append_len_prefixed(payload, block.server_pubkey);
    append_len_prefixed(payload, block.mission_id);
    append_len_prefixed(payload, block.policy_hash);
    append_len_prefixed(payload, block.last_block_hash);

    bool valid = Dilithium::verify(server_pubkey, payload, block.signature);
    
    if (!valid) {
        throw InvalidSignature("Signature serveur invalide");
    }
    
    if (block.last_block_hash != last_block_hash) {
        throw AuthenticationFailed("Chaînage invalide");
    }
    
    last_block_hash = hash_data(payload);
    state = DroneState::SERVER_AUTH_VERIFIED;
    
    secure_channel = std::make_unique<SecureChannel>(session_key, SecureChannelRole::DroneClient);
    
    std::vector<uint8_t> video_key = derive_video_key(session_key);
    video_channel = std::make_unique<VideoChannel>(video_key, 1);
    
    std::vector<uint8_t> plaintext = {0x62, 0x65, 0x74, 0x61}; // "beta"
    encrypted_msg = secure_channel->encrypt(plaintext, last_block_hash).raw;
    
    return encrypted_msg;
}

std::vector<uint8_t> HesiaDrone::build_confirm() {
    if (state != DroneState::SERVER_AUTH_VERIFIED) {
        throw InvalidStateError(drone_state_to_string(state), "SERVER_AUTH_VERIFIED");
    }
    
    std::vector<uint8_t> ok_bytes = {0x4F, 0x4B}; // "OK"
    std::vector<uint8_t> payload;
    payload.reserve(ok_bytes.size() + last_block_hash.size());
    payload.insert(payload.end(), ok_bytes.begin(), ok_bytes.end());
    payload.insert(payload.end(), last_block_hash.begin(), last_block_hash.end());
    
    // ✅ Signer avec la clé privée du drone (pas celle du serveur)
    std::vector<uint8_t> signature = sign_with_drone_identity(payload);

    return signature;
}

void HesiaDrone::finalize_confirm_ok(const std::vector<uint8_t>& response) {
    if (state != DroneState::SERVER_AUTH_VERIFIED) {
        throw InvalidStateError(drone_state_to_string(state), "SERVER_AUTH_VERIFIED");
    }
    static const std::vector<uint8_t> kExpectedOk = {'O', 'K'};
    if (response != kExpectedOk) {
        throw AuthenticationFailed("CONFIRM final invalide");
    }
    state = DroneState::SECURE_SESSION;
}

std::vector<uint8_t> HesiaDrone::send_secure_message(const std::string& msg_type, const std::string& json_data) {
    // Sérialise l'accès à l'état de chaînage (last_block_hash, seq). Garantit
    // l'absence de course de données si plusieurs threads émettent des messages
    // sécurisés. L'ordre fil == ordre de chaînage est assuré par l'appelant
    // (DroneNetworkClient::enqueue_secure_message) qui maintient ce verrou
    // pendant la mise en file.
    std::lock_guard<std::mutex> lock(secure_message_mutex_);

    if (state != DroneState::SECURE_SESSION) {
        throw SessionNotEstablished("Session sécurisée non active");
    }

    // Construire le payload JSON (simplifié)
    std::stringstream ss;
    ss << "{\"type\":\"" << msg_type << "\",\"seq\":" << seq << ",\"data\":" << json_data << "}";
    std::string json_payload = ss.str();
    
    std::vector<uint8_t> plaintext(json_payload.begin(), json_payload.end());
    
    EncryptedMessage encrypted = secure_channel->encrypt(plaintext, last_block_hash);
    
    std::vector<uint8_t> combined;
    combined.reserve(last_block_hash.size() + plaintext.size());
    combined.insert(combined.end(), last_block_hash.begin(), last_block_hash.end());
    combined.insert(combined.end(), plaintext.begin(), plaintext.end());
    
    last_block_hash = hash_data(combined);
    seq++;

    SecureMemory::zeroize(plaintext);
    SecureMemory::zeroize(combined);
    SecureMemory::zeroize(json_payload);
    return encrypted.raw;
}


void HesiaDrone::set_tls_exporter_secret(const std::vector<uint8_t>& secret) {
    // Transport-layer binding material (TLS exporter)
    SecureMemory::zeroize(tls_exporter_secret);
    tls_exporter_secret = secret;
    use_tls_exporter_binding = !tls_exporter_secret.empty();
}

void HesiaDrone::set_tls_peer_cert_sha256(const std::vector<uint8_t>& digest) {
    // SHA-256 over peer certificate DER as observed by the drone via TLS
    SecureMemory::zeroize(tls_peer_cert_sha256);
    tls_peer_cert_sha256 = digest;
}


} // namespace hesia
