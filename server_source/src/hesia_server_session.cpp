#include "hesia_server_session.hpp"

#include <chrono>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <unordered_set>
#include <algorithm>

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/kdf.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "net_framing.hpp"
#include "tls_utils.hpp"
#include "security_audit.hpp"
#include "telemetry_parser.hpp"
#include "../../drone_source/serialization.hpp"
#include "../../drone_source/crypto_real.hpp"
#include "../../drone_source/optee_client.hpp"
#include "../../drone_source/security_utils.hpp"
#include "../../drone_source/exceptions.hpp"

namespace hesia {

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

static bool forensic_capture_enabled(const SecurityPolicy& policy, const char* env_name) {
#ifdef HESIA_ALLOW_FORENSIC_CAPTURE
    return env_flag_enabled(env_name);
#else
    if (policy.prod_fuse) {
        return false;
    }
    return env_flag_enabled(env_name);
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
    if (compact.empty()) {
        return {};
    }
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

static void append_u32_be(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

static void append_len_prefixed(std::vector<uint8_t>& out, const std::vector<uint8_t>& value) {
    append_u32_be(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static void append_len_prefixed(std::vector<uint8_t>& out, const std::string& value) {
    append_u32_be(out, static_cast<uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static std::vector<uint8_t> build_signed_drone_auth_payload(const BlockDroneAuth& auth) {
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

static std::vector<uint8_t> build_signed_server_auth_payload(const BlockServerAuth& auth) {
    std::vector<uint8_t> payload;
    payload.reserve(auth.server_id.size() + auth.server_pubkey.size() + auth.mission_id.size() +
                    auth.policy_hash.size() + auth.last_block_hash.size() + 8 * 5);
    append_len_prefixed(payload, auth.server_id);
    append_len_prefixed(payload, auth.server_pubkey);
    append_len_prefixed(payload, auth.mission_id);
    append_len_prefixed(payload, auth.policy_hash);
    append_len_prefixed(payload, auth.last_block_hash);
    return payload;
}

static std::vector<uint8_t> compute_policy_hash_or_throw(const SecurityPolicy& policy) {
    const std::filesystem::path policy_path(policy.policy_path);
    std::vector<uint8_t> policy_bytes = read_file_binary(policy_path);
    if (policy_bytes.empty()) {
        throw std::runtime_error("Policy file is empty: " + policy_path.string());
    }
    return hash_data(policy_bytes);
}

static std::string sanitize_path_component(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "client";
    }
    return out;
}

static void harden_path_permissions(const std::filesystem::path& path,
                                    std::filesystem::perms perms) {
#ifndef _WIN32
    std::error_code ec;
    std::filesystem::permissions(path, perms, std::filesystem::perm_options::replace, ec);
    (void)ec;
#else
    (void)path;
    (void)perms;
#endif
}

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

static bool interval_elapsed(uint64_t now, uint64_t& last_ms, uint64_t interval_ms) {
    if (last_ms == 0 || now < last_ms || (now - last_ms) >= interval_ms) {
        last_ms = now;
        return true;
    }
    return false;
}

// Fonction pour sauvegarder les frames déchiffrées
static void save_decrypted_frame(const std::vector<uint8_t>& frame_data,
                                 uint64_t frame_id,
                                 const std::string& client_id,
                                 const std::filesystem::path& base_dir) {
    try {
        if (base_dir.empty()) {
            return;
        }
        // Creer le dossier forensic s'il n'existe pas
        std::filesystem::path frames_dir = base_dir;
        if (!std::filesystem::exists(frames_dir)) {
            std::filesystem::create_directories(frames_dir);
            harden_path_permissions(frames_dir, std::filesystem::perms::owner_all);
        }

        // Creer un sous-dossier pour ce client
        std::filesystem::path client_dir = frames_dir / sanitize_path_component(client_id);
        if (!std::filesystem::exists(client_dir)) {
            std::filesystem::create_directories(client_dir);
            harden_path_permissions(client_dir, std::filesystem::perms::owner_all);
        }

        // Nom de fichier avec timestamp et frame_id
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::string filename = client_dir / ("frame_" + std::to_string(frame_id) + "_" + std::to_string(timestamp) + ".bin");

        // Sauvegarder les donnees brutes
        std::ofstream file(filename, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(frame_data.data()), frame_data.size());
            file.close();
            harden_path_permissions(filename, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        }

        // Sauvegarder aussi une version hex pour inspection
        std::string hex_filename = client_dir / ("frame_" + std::to_string(frame_id) + "_" + std::to_string(timestamp) + ".hex");
        std::ofstream hex_file(hex_filename);
        if (hex_file.is_open()) {
            hex_file << std::hex << std::setfill('0');
            for (size_t i = 0; i < frame_data.size(); ++i) {
                hex_file << std::setw(2) << static_cast<int>(frame_data[i]);
                if ((i + 1) % 32 == 0) hex_file << "\n";
                else if ((i + 1) % 4 == 0) hex_file << " ";
            }
            hex_file.close();
            harden_path_permissions(hex_filename, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        }

    } catch (const std::exception& e) {
        // Ne pas echouer si la sauvegarde echoue, juste logger
        // (le logger n'est pas disponible ici, donc on ignore silencieusement)
        (void)e;
    }
}

static void write_binary_atomic(const std::filesystem::path& path,
                                const std::vector<uint8_t>& data) {
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
            harden_path_permissions(path.parent_path(), std::filesystem::perms::owner_all);
        }
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return;
        if (!data.empty()) {
            f.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        f.close();
        harden_path_permissions(tmp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
        } else {
            harden_path_permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        }
    } catch (...) {
        // best-effort
    }
}

static void write_text_atomic(const std::filesystem::path& path, const std::string& text) {
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
            harden_path_permissions(path.parent_path(), std::filesystem::perms::owner_all);
        }
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        harden_path_permissions(tmp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
        } else {
            harden_path_permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        }
    } catch (...) {
        // best-effort
    }
}

static void write_telemetry_json(const std::filesystem::path& ui_dir,
                                 const std::string& drone_id,
                                 const std::string& payload) {
    if (ui_dir.empty()) return;
    const ParsedTelemetryJson telemetry = parse_telemetry_json_payload(payload, now_ms());

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"ts_ms\":" << telemetry.ts_ms
        << ",\"drone_id\":\"" << drone_id << "\""
        << ",\"cpu_temp_c\":" << telemetry.cpu_temp_c
        << ",\"cpu_usage_pct\":" << telemetry.cpu_usage_pct
        << ",\"ram_used_mb\":" << telemetry.ram_used_mb
        << ",\"ram_total_mb\":" << telemetry.ram_total_mb
        << ",\"voltage_v\":" << telemetry.voltage_v
        << ",\"current_a\":" << telemetry.current_a
        << ",\"power_w\":" << telemetry.power_w
        << ",\"gps_lat\":" << telemetry.gps_lat
        << ",\"gps_lon\":" << telemetry.gps_lon
        << ",\"gps_alt_m\":" << telemetry.gps_alt_m
        << "}";

    write_text_atomic(ui_dir / "telemetry.json", oss.str());
}

static std::string hex_prefix(const std::vector<uint8_t>& v, size_t n=16) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < v.size() && i < n; ++i) {
        oss << std::setw(2) << static_cast<int>(v[i]);
    }
    if (v.size() > n) oss << "...";
    return oss.str();
}

static std::string bytes_to_hex(const std::vector<uint8_t>& v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : v) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

static std::vector<uint8_t> sha256_bytes(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP sha256 init/update failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != SHA256_DIGEST_LENGTH) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP sha256 final failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

static std::vector<uint8_t> ecdsa_rs_to_der(const std::vector<uint8_t>& signature) {
    if (signature.size() != 64) {
        throw std::runtime_error("Invalid raw ECDSA signature size");
    }

    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (!sig) {
        throw std::runtime_error("ECDSA_SIG_new failed");
    }

    BIGNUM* r = BN_bin2bn(signature.data(), 32, nullptr);
    BIGNUM* s = BN_bin2bn(signature.data() + 32, 32, nullptr);
    if (!r || !s || ECDSA_SIG_set0(sig, r, s) != 1) {
        if (r) BN_free(r);
        if (s) BN_free(s);
        ECDSA_SIG_free(sig);
        throw std::runtime_error("ECDSA_SIG_set0 failed");
    }

    const int der_len = i2d_ECDSA_SIG(sig, nullptr);
    if (der_len <= 0) {
        ECDSA_SIG_free(sig);
        throw std::runtime_error("i2d_ECDSA_SIG size failed");
    }

    std::vector<uint8_t> der(static_cast<size_t>(der_len));
    unsigned char* der_ptr = der.data();
    if (i2d_ECDSA_SIG(sig, &der_ptr) != der_len) {
        ECDSA_SIG_free(sig);
        throw std::runtime_error("i2d_ECDSA_SIG failed");
    }

    ECDSA_SIG_free(sig);
    return der;
}

static EVP_PKEY* load_p256_pubkey_from_uncompressed(const std::vector<uint8_t>& pubkey) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    if (!ctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_from_name failed");
    }
    if (EVP_PKEY_fromdata_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_fromdata_init failed");
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
        throw std::runtime_error("EVP_PKEY_fromdata failed");
    }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static bool verify_p256_attestation_signature(const std::vector<uint8_t>& pubkey,
                                              const std::vector<uint8_t>& digest,
                                              const std::vector<uint8_t>& signature) {
    if (pubkey.size() != 65 || pubkey[0] != 0x04 || signature.size() != 64 || digest.empty()) {
        return false;
    }

    EVP_PKEY* pkey = load_p256_pubkey_from_uncompressed(pubkey);
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_PKEY_CTX_new failed");
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

static bool verify_tee_attestation_signature(const std::vector<uint8_t>& pubkey,
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
        return verify_p256_attestation_signature(pubkey, signed_data, signature);
    }

    try {
        return Dilithium::verify(pubkey, signed_data, signature);
    } catch (const std::invalid_argument&) {
        return false;
    }
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

static std::string normalize_hex_line(const std::string& line) {
    std::string t = trim_copy(line);
    if (t.empty()) return t;
    if (t[0] == '#') return std::string();
    const std::string sha256_prefix = "sha256:";
    const std::string sha3512_prefix = "sha3-512:";
    if (t.rfind(sha256_prefix, 0) == 0) {
        t = t.substr(sha256_prefix.size());
    } else if (t.rfind(sha3512_prefix, 0) == 0) {
        t = t.substr(sha3512_prefix.size());
    }
    t = trim_copy(t);
    t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }), t.end());
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return t;
}

static std::unordered_set<std::string> load_hex_list(const std::string& path, size_t expected_bytes) {
    std::unordered_set<std::string> out;
    if (!std::filesystem::exists(path)) return out;
    std::ifstream f(path);
    if (!f.is_open()) return out;
    std::string line;
    const size_t expected_len = expected_bytes * 2;
    while (std::getline(f, line)) {
        std::string hex = normalize_hex_line(line);
        if (hex.empty()) continue;
        if (hex.size() != expected_len) continue;
        out.insert(hex);
    }
    return out;
}

static std::filesystem::path resolve_allowlist_signing_pubkey(const std::string& secure_dir,
                                                              const std::string& keys_dir,
                                                              bool prod_fuse) {
    const std::filesystem::path secure_candidate = std::filesystem::path(secure_dir) / "allowlist_signing.pub";
    if (std::filesystem::exists(secure_candidate)) {
        return secure_candidate;
    }
    const std::filesystem::path keys_candidate = std::filesystem::path(keys_dir) / "allowlist_signing.pub";
    if (!prod_fuse && std::filesystem::exists(keys_candidate)) {
        return keys_candidate;
    }
    return secure_candidate;
}

static std::unordered_set<std::string> load_signed_hex_list_or_throw(const std::filesystem::path& path,
                                                                     size_t expected_bytes,
                                                                     bool require_signature,
                                                                     const std::filesystem::path& signing_pubkey_path) {
    std::unordered_set<std::string> out;
    if (!std::filesystem::exists(path)) {
        return out;
    }

    const std::vector<uint8_t> raw = read_file_binary(path);
    const std::filesystem::path sig_path = path.string() + ".sig";
    const bool have_sig = std::filesystem::exists(sig_path);
    if (require_signature || have_sig) {
        if (!have_sig) {
            throw SecurityViolation("Missing detached signature for control list: " + path.string());
        }
        if (!std::filesystem::exists(signing_pubkey_path)) {
            throw SecurityViolation("Missing allowlist signing public key: " + signing_pubkey_path.string());
        }
        const std::vector<uint8_t> sig_raw = read_file_binary(sig_path);
        const std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;
        if (!verify_ed25519_signature(raw, sig, signing_pubkey_path)) {
            throw SecurityViolation("Invalid detached signature for control list: " + path.string());
        }
    }

    const size_t expected_len = expected_bytes * 2;
    std::string text(raw.begin(), raw.end());
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        std::string hex = normalize_hex_line(line);
        if (hex.empty()) continue;
        if (hex.size() != expected_len) continue;
        out.insert(hex);
    }
    return out;
}

class RateLimiter {
public:
    RateLimiter(std::uint64_t rate_bytes_per_sec, std::uint64_t burst_bytes)
        : rate_per_sec_(rate_bytes_per_sec),
          burst_bytes_(burst_bytes ? burst_bytes : rate_bytes_per_sec),
          tokens_(static_cast<double>(burst_bytes_)),
          last_refill_(std::chrono::steady_clock::now()) {}

    bool consume(std::size_t bytes) {
        if (rate_per_sec_ == 0) {
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_refill_).count();
        if (elapsed > 0) {
            tokens_ = std::min<double>(static_cast<double>(burst_bytes_),
                                       tokens_ + elapsed * static_cast<double>(rate_per_sec_));
            last_refill_ = now;
        }
        if (tokens_ < static_cast<double>(bytes)) {
            return false;
        }
        tokens_ -= static_cast<double>(bytes);
        return true;
    }

private:
    std::uint64_t rate_per_sec_;
    std::uint64_t burst_bytes_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

HesiaServerSession::HesiaServerSession(SSL* ssl,
                                       const std::string& client_label,
                                       const std::string& keys_dir,
                                       const std::string& secure_dir,
                                       std::shared_ptr<Logger> logger,
                                       std::shared_ptr<SecurityAudit> audit,
                                       const SecurityPolicy& policy)
    : ssl_(ssl),
      client_label_(client_label),
      keys_dir_(keys_dir),
      secure_dir_(secure_dir),
      log_(std::move(logger)),
      audit_(std::move(audit)),
      policy_(policy) {
    last_block_hash_.assign(64, 0x00);
    rate_limiter_ = std::make_unique<RateLimiter>(policy_.rate_limit_bps, policy_.rate_limit_burst);
}

HesiaServerSession::~HesiaServerSession() {
    SecureMemory::zeroize(tls_exporter_secret_);
    SecureMemory::zeroize(tls_server_cert_sha256_);
    SecureMemory::zeroize(server_sk_);
    SecureMemory::zeroize(server_pk_);
    SecureMemory::zeroize(kyber_pk_);
    SecureMemory::zeroize(kyber_sk_);
    SecureMemory::zeroize(nonce_s_);
    SecureMemory::zeroize(session_id_);
    SecureMemory::zeroize(drone_pubkey_);
    SecureMemory::zeroize(expected_drone_pubkey_);
    SecureMemory::zeroize(expected_drone_tee_pubkey_);
    SecureMemory::zeroize(session_key_);
    SecureMemory::zeroize(last_block_hash_);
    SecureMemory::zeroize(t_hello_);
    SecureMemory::zeroize(t_hello_ack_);
    SecureMemory::zeroize(t_key_init_);
    SecureMemory::zeroize(t_key_resp_);
    SecureMemory::zeroize(transcript_hash_);
}

void HesiaServerSession::run() {
    if (audit_) {
        audit_->event("SESSION_START", "INFO", "client=" + client_label_);
    }
    // Require TLS exporter + cert digest (max alignment / hybrid binding required).
    tls_exporter_secret_ = tls_exporter_32(ssl_);
    if (tls_exporter_secret_.size() != 32) {
        throw std::runtime_error("TLS exporter length invalid");
    }
    tls_server_cert_sha256_ = tls_server_cert_sha256_der(ssl_);
    if (tls_server_cert_sha256_.size() != 32) {
        throw std::runtime_error("TLS server cert SHA256 length invalid");
    }

    log_->info("TLS exporter OK (32B), cert_sha256(DER)=" + hex_prefix(tls_server_cert_sha256_, 16));
    if (audit_) {
        audit_->event("TLS_BINDING_OK", "INFO", "client=" + client_label_);
    }

    load_server_keys();
    init_kyber();

    step_hello();
    step_key_exchange();
    step_drone_auth();
    step_confirm();

    secure_loop();
}

void HesiaServerSession::load_server_keys() {
    auto read_bin = [](const std::filesystem::path& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open key file: " + path.string());
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.empty()) throw std::runtime_error("Empty key file: " + path.string());
        return buf;
    };

    server_signing_in_tee_ = false;
    SecureMemory::zeroize(server_sk_);

    if (optee_available()) {
        try {
            if (optee_mldsa_signing_ready(OpteeMldsaSlot::ServerIdentity)) {
                server_pk_ = optee_get_mldsa_public_key(OpteeMldsaSlot::ServerIdentity);
                if (server_pk_.empty()) {
                    throw std::runtime_error("TEE server ML-DSA public key export returned empty key");
                }
                server_signing_in_tee_ = true;
                log_->info("Server ML-DSA identity loaded from OP-TEE slot 'server' (pk=" +
                           std::to_string(server_pk_.size()) + "B)");
                load_drone_pubkey();
                return;
            }
        } catch (const std::exception& e) {
            log_->warning(std::string("Server OP-TEE ML-DSA slot unavailable: ") + e.what());
        }
    }

#ifndef HESIA_ALLOW_SOFT_SIGN
    throw std::runtime_error("Server ML-DSA signing must come from OP-TEE slot 'server'; REE soft-sign is disabled in this build");
#else

    const bool allow_demo_server_keys = env_flag_enabled("HESIA_ALLOW_DEMO_SERVER_KEYS");
    struct KeyPairCandidate {
        const char* secret_name;
        const char* public_name;
        bool demo;
    };
    std::vector<KeyPairCandidate> candidates = {
        {"server_secret.bin", "server_public.bin", false},
        {"mldsa87_secret.bin", "mldsa87_public.bin", false},
    };
    if (!policy_.prod_fuse && allow_demo_server_keys) {
        candidates.push_back({"demo_secret.bin", "demo_public.bin", true});
    }

    struct SearchRoot {
        std::filesystem::path dir;
        bool is_secure;
    };

    std::vector<SearchRoot> roots = {
        {std::filesystem::path(secure_dir_), true},
    };
    if (!policy_.prod_fuse) {
        roots.push_back({std::filesystem::path(keys_dir_), false});
    }

    std::filesystem::path selected_secret;
    std::filesystem::path selected_public;
    bool using_demo_pair = false;
    bool selected_secure_root = false;
    for (const auto& root : roots) {
        for (const auto& candidate : candidates) {
            const std::filesystem::path secret_path = root.dir / candidate.secret_name;
            const std::filesystem::path public_path = root.dir / candidate.public_name;
            if (std::filesystem::exists(secret_path) && std::filesystem::exists(public_path)) {
                selected_secret = secret_path;
                selected_public = public_path;
                using_demo_pair = candidate.demo;
                selected_secure_root = root.is_secure;
                break;
            }
        }
        if (!selected_secret.empty()) {
            break;
        }
    }

    if (selected_secret.empty() || selected_public.empty()) {
        std::string msg =
            "Missing server ML-DSA keypair. Expected server_secret.bin/server_public.bin or mldsa87_* files in secure_dir";
        if (!policy_.prod_fuse) {
            msg += " (non-production fallback: keys_dir)";
        }
        msg += ".";
        throw std::runtime_error(msg);
    }
    if (policy_.prod_fuse && using_demo_pair) {
        throw std::runtime_error("Refusing demo ML-DSA server keys in production mode");
    }
    if (policy_.prod_fuse && !selected_secure_root) {
        throw std::runtime_error("Production mode requires ML-DSA server keys in secure_dir");
    }

    server_sk_ = read_bin(selected_secret);
    server_pk_ = read_bin(selected_public);
    (void)SecureMemory::protect(server_sk_);

    log_->info("Server ML-DSA keys loaded (pk=" + std::to_string(server_pk_.size()) +
               "B, sk=" + std::to_string(server_sk_.size()) + "B) from " + selected_secret.parent_path().string());

    load_drone_pubkey();
#endif
}

std::vector<uint8_t> HesiaServerSession::sign_with_server_identity(const std::vector<uint8_t>& payload)
{
    if (payload.empty()) {
        throw std::runtime_error("Server signing payload is empty");
    }

    if (server_signing_in_tee_) {
        return optee_sign_mldsa_payload(payload, OpteeMldsaSlot::ServerIdentity);
    }

#ifdef HESIA_ALLOW_SOFT_SIGN
    if (server_sk_.empty()) {
        throw std::runtime_error("Server ML-DSA secret key is not loaded");
    }
    return Dilithium::sign(server_sk_, payload);
#else
    throw std::runtime_error("Server ML-DSA signing requested outside OP-TEE while soft-sign is disabled");
#endif
}

void HesiaServerSession::load_drone_pubkey() {
    auto read_bin = [](const std::string& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open key file: " + path);
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (buf.empty()) throw std::runtime_error("Empty key file: " + path);
        return buf;
    };

    std::string path;
    if (!policy_.drone_pubkey_file.empty()) {
        std::filesystem::path configured(policy_.drone_pubkey_file);
        if (configured.is_absolute()) {
            path = configured.string();
        } else {
            const std::filesystem::path secure_candidate = std::filesystem::path(secure_dir_) / configured;
            const std::filesystem::path keys_candidate = std::filesystem::path(keys_dir_) / configured;
            if (std::filesystem::exists(secure_candidate)) {
                path = secure_candidate.string();
            } else if (!policy_.prod_fuse && std::filesystem::exists(keys_candidate)) {
                path = keys_candidate.string();
            } else {
                path = secure_candidate.string();
            }
        }
    } else {
        const std::vector<std::filesystem::path> candidates = {
            std::filesystem::path(secure_dir_) / "drone_public.bin",
            std::filesystem::path(secure_dir_) / "dilithium5_pk.bin",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                path = candidate.string();
                break;
            }
        }
        if (path.empty() && !policy_.prod_fuse) {
            const std::vector<std::filesystem::path> fallback_candidates = {
                std::filesystem::path(keys_dir_) / "drone_public.bin",
                std::filesystem::path(keys_dir_) / "dilithium5_pk.bin",
            };
            for (const auto& candidate : fallback_candidates) {
                if (std::filesystem::exists(candidate)) {
                    path = candidate.string();
                    break;
                }
            }
        }
    }

    if (!path.empty()) {
        std::filesystem::path p(path);
        if (!p.is_absolute()) {
            path = (std::filesystem::path(keys_dir_) / p).string();
        }
    }

    require_pinned_drone_key_ = policy_.prod_fuse || policy_.require_pinned_drone_pubkey || !path.empty();

    if (!path.empty()) {
        expected_drone_pubkey_ = read_bin(path);
        log_->info("Pinned drone public key loaded (" + std::to_string(expected_drone_pubkey_.size()) +
                   "B) from " + path);
    } else {
        if (require_pinned_drone_key_) {
            throw std::runtime_error("Pinned drone public key required but not found. "
                                     "Set policy drone_pubkey_file or place drone_public.bin in secure_dir.");
        }
        log_->warning("No pinned drone public key configured; accepting key from DRONE_AUTH");
    }

    std::string tee_path;
    if (!policy_.drone_tee_pubkey_file.empty()) {
        std::filesystem::path configured(policy_.drone_tee_pubkey_file);
        if (configured.is_absolute()) {
            tee_path = configured.string();
        } else {
            const std::filesystem::path secure_candidate = std::filesystem::path(secure_dir_) / configured;
            const std::filesystem::path keys_candidate = std::filesystem::path(keys_dir_) / configured;
            if (std::filesystem::exists(secure_candidate)) {
                tee_path = secure_candidate.string();
            } else if (!policy_.prod_fuse && std::filesystem::exists(keys_candidate)) {
                tee_path = keys_candidate.string();
            } else {
                tee_path = secure_candidate.string();
            }
        }
    } else {
        const std::vector<std::filesystem::path> candidates = {
            std::filesystem::path(secure_dir_) / "drone_tee_attest_pub.bin",
            std::filesystem::path(secure_dir_) / "tee_attest_mldsa_pub.bin",
            std::filesystem::path(secure_dir_) / "tee_attest_p256_pub.bin",
            std::filesystem::path(secure_dir_) / "dilithium5_pk.bin",
            std::filesystem::path(secure_dir_) / "drone_public.bin",
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                tee_path = candidate.string();
                break;
            }
        }
        if (tee_path.empty() && !policy_.prod_fuse) {
            const std::vector<std::filesystem::path> fallback_candidates = {
                std::filesystem::path(keys_dir_) / "drone_tee_attest_pub.bin",
                std::filesystem::path(keys_dir_) / "tee_attest_mldsa_pub.bin",
                std::filesystem::path(keys_dir_) / "tee_attest_p256_pub.bin",
                std::filesystem::path(keys_dir_) / "dilithium5_pk.bin",
                std::filesystem::path(keys_dir_) / "drone_public.bin",
            };
            for (const auto& candidate : fallback_candidates) {
                if (std::filesystem::exists(candidate)) {
                    tee_path = candidate.string();
                    break;
                }
            }
        }
    }

    if (!tee_path.empty()) {
        expected_drone_tee_pubkey_ = read_bin(tee_path);
        if (expected_drone_tee_pubkey_.empty()) {
            throw std::runtime_error("Pinned drone TEE public key must not be empty");
        }
        log_->info("Pinned drone TEE public key loaded (" + std::to_string(expected_drone_tee_pubkey_.size()) +
                   "B) from " + tee_path);
    } else if (policy_.require_tee_attestation && !expected_drone_pubkey_.empty()) {
        expected_drone_tee_pubkey_ = expected_drone_pubkey_;
        log_->info("No dedicated pinned TEE public key found; reusing pinned drone ML-DSA public key");
    } else if (policy_.require_tee_attestation && policy_.prod_fuse) {
        throw std::runtime_error("Pinned drone TEE public key required but not found. "
                                 "Set policy drone_tee_pubkey_file or place a TA-exported public key in secure_dir.");
    } else if (policy_.require_tee_attestation) {
        log_->warning("No pinned drone TEE public key configured; dev mode will trust DRONE_AUTH attestation key");
    }
}

void HesiaServerSession::init_kyber() {
    auto kp = Kyber::generate_keypair();
    kyber_pk_ = std::move(kp.first);
    kyber_sk_ = std::move(kp.second);
    log_->info(std::string("Kyber: ") + Kyber::ALG + " (pk=" + std::to_string(kyber_pk_.size()) +
               "B, sk=" + std::to_string(kyber_sk_.size()) + "B)");
}

std::vector<uint8_t> HesiaServerSession::hkdf_session_key(const std::vector<uint8_t>& shared,
                                                          const std::vector<uint8_t>& nonce_s,
                                                          const std::vector<uint8_t>& nonce_d,
                                                          const std::vector<uint8_t>& session_id,
                                                          const std::vector<uint8_t>& tls_exporter_32) {
    EVP_PKEY_CTX* kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!kdf) throw std::runtime_error("EVP_PKEY_CTX_new_id failed");

    auto fail = [&](const char* msg) {
        EVP_PKEY_CTX_free(kdf);
        throw std::runtime_error(msg);
    };

    if (EVP_PKEY_derive_init(kdf) <= 0) fail("EVP_PKEY_derive_init failed");
    if (EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha3_512()) <= 0) fail("EVP_PKEY_CTX_set_hkdf_md failed");

    // Salt = nonce_s || nonce_d || session_id || tls_exporter(32)  (required)
    std::vector<uint8_t> salt;
    salt.reserve(nonce_s.size() + nonce_d.size() + session_id.size() + 32);
    salt.insert(salt.end(), nonce_s.begin(), nonce_s.end());
    salt.insert(salt.end(), nonce_d.begin(), nonce_d.end());
    salt.insert(salt.end(), session_id.begin(), session_id.end());
    if (tls_exporter_32.size() != 32) fail("tls_exporter_32 invalid size");
    salt.insert(salt.end(), tls_exporter_32.begin(), tls_exporter_32.end());

    if (EVP_PKEY_CTX_set1_hkdf_salt(kdf, salt.data(), static_cast<int>(salt.size())) <= 0) fail("set1_hkdf_salt failed");
    if (EVP_PKEY_CTX_set1_hkdf_key(kdf, shared.data(), static_cast<int>(shared.size())) <= 0) fail("set1_hkdf_key failed");

    const std::string info = "HESIA_SESSION_KEY_v1";
    if (EVP_PKEY_CTX_add1_hkdf_info(kdf,
        reinterpret_cast<const unsigned char*>(info.data()),
        static_cast<int>(info.size())) <= 0) fail("add1_hkdf_info failed");

    std::vector<uint8_t> out(32);
    size_t out_len = out.size();
    if (EVP_PKEY_derive(kdf, out.data(), &out_len) <= 0 || out_len != out.size()) fail("EVP_PKEY_derive failed");
    EVP_PKEY_CTX_free(kdf);
    return out;
}

void HesiaServerSession::step_hello() {
    std::vector<uint8_t> hello_bytes;
    if (!recv_frame(ssl_, hello_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive HELLO");
    }
    t_hello_ = hello_bytes;

    Hello hello = Serializer::deserialize_hello(hello_bytes);

    if ((hello.features & 0x01u) == 0u) {
        throw std::runtime_error("Client does not speak HESIA v1");
    }

    // Require exporter binding support for maximal alignment (hybrid is mandatory).
    if ((hello.features & 0x02u) == 0u) {
        throw std::runtime_error("Client HELLO missing exporter support (0x02). TLS hybrid required.");
    }

    // HELLO_ACK.response_hash = SHA3-512(hello.random_64 || "DRONE_SALT")
    const std::string salt_str = "DRONE_SALT";
    std::vector<uint8_t> input;
    input.reserve(hello.random_64.size() + salt_str.size());
    input.insert(input.end(), hello.random_64.begin(), hello.random_64.end());
    input.insert(input.end(), salt_str.begin(), salt_str.end());
    std::vector<uint8_t> response_hash = hash_data(input);

    HelloAck ack;
    ack.response_hash = response_hash;

    // Capabilities: 0x02 => exporter binding enabled (both sides support it and server has exporter)
    ack.capabilities = 0x02u;

    std::vector<uint8_t> ack_bytes = Serializer::serialize_hello_ack(ack);
    t_hello_ack_ = ack_bytes;

    if (!send_frame(ssl_, ack_bytes)) {
        throw std::runtime_error("Failed to send HELLO_ACK");
    }

    // Build KEY_INIT
    nonce_s_.assign(16, 0);
    if (RAND_bytes(nonce_s_.data(), static_cast<int>(nonce_s_.size())) != 1) {
        throw std::runtime_error("RAND_bytes nonce_s failed");
    }

    session_id_.assign(16, 0);
    if (RAND_bytes(session_id_.data(), static_cast<int>(session_id_.size())) != 1) {
        throw std::runtime_error("RAND_bytes session_id failed");
    }

    const uint64_t ts = now_ms();

    // context_hash = SHA3-512("HESIA_CTX_v1" || kyber_pk || nonce_s || session_id)
    static const std::string ctx_label = "HESIA_CTX_v1";
    std::vector<uint8_t> ctx;
    ctx.reserve(ctx_label.size() + kyber_pk_.size() + nonce_s_.size() + session_id_.size());
    ctx.insert(ctx.end(), ctx_label.begin(), ctx_label.end());
    ctx.insert(ctx.end(), kyber_pk_.begin(), kyber_pk_.end());
    ctx.insert(ctx.end(), nonce_s_.begin(), nonce_s_.end());
    ctx.insert(ctx.end(), session_id_.begin(), session_id_.end());
    std::vector<uint8_t> context_hash = hash_data(ctx);

    KeyInit init;
    init.kyber_pubkey = kyber_pk_;
    init.nonce_s = nonce_s_;
    init.session_id = session_id_;
    init.timestamp = ts;
    init.context_hash = context_hash;

    std::vector<uint8_t> init_bytes = Serializer::serialize_key_init(init);
    t_key_init_ = init_bytes;

    if (!send_frame(ssl_, init_bytes)) {
        throw std::runtime_error("Failed to send KEY_INIT");
    }

    state_ = ServerState::KEY_INIT_SENT;
    log_->info("HELLO/ACK/KEY_INIT ok (session_id len=" + std::to_string(session_id_.size()) + ")");
}

void HesiaServerSession::step_key_exchange() {
    if (state_ != ServerState::KEY_INIT_SENT) {
        throw std::runtime_error("Invalid state before KEY_RESP");
    }

    std::vector<uint8_t> key_resp_bytes;
    if (!recv_frame(ssl_, key_resp_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive KEY_RESP");
    }
    t_key_resp_ = key_resp_bytes;

    KeyResp resp = Serializer::deserialize_key_resp(key_resp_bytes);

    if (!ConstantTime::equals(resp.session_id, session_id_)) {
        throw std::runtime_error("KEY_RESP session_id mismatch");
    }

    if (resp.nonce_d.size() < 16 || resp.nonce_d.size() > 64) {
        throw std::runtime_error("KEY_RESP nonce_d invalid size");
    }

    // Decapsulate
    std::vector<uint8_t> shared = Kyber::decaps(kyber_sk_, resp.kyber_ciphertext);

    // Derive session_key (AES-256 key)
    session_key_ = hkdf_session_key(shared, nonce_s_, resp.nonce_d, session_id_, tls_exporter_secret_);

    // Verify response_hash = SHA3-512(ciphertext||nonce_d||session_id||session_key)
    std::vector<uint8_t> rh_data;
    rh_data.reserve(resp.kyber_ciphertext.size() + resp.nonce_d.size() + resp.session_id.size() + session_key_.size());
    rh_data.insert(rh_data.end(), resp.kyber_ciphertext.begin(), resp.kyber_ciphertext.end());
    rh_data.insert(rh_data.end(), resp.nonce_d.begin(), resp.nonce_d.end());
    rh_data.insert(rh_data.end(), resp.session_id.begin(), resp.session_id.end());
    rh_data.insert(rh_data.end(), session_key_.begin(), session_key_.end());
    std::vector<uint8_t> expected_rh = hash_data(rh_data);

    if (!ConstantTime::equals(expected_rh, resp.response_hash)) {
        throw SecurityViolation("Response hash invalide - manipulation détectée");
    }

    // Transcript = HELLO || HELLO_ACK || KEY_INIT || KEY_RESP
    std::vector<uint8_t> transcript;
    transcript.reserve(t_hello_.size() + t_hello_ack_.size() + t_key_init_.size() + t_key_resp_.size());
    transcript.insert(transcript.end(), t_hello_.begin(), t_hello_.end());
    transcript.insert(transcript.end(), t_hello_ack_.begin(), t_hello_ack_.end());
    transcript.insert(transcript.end(), t_key_init_.begin(), t_key_init_.end());
    transcript.insert(transcript.end(), t_key_resp_.begin(), t_key_resp_.end());
    transcript_hash_ = hash_data(transcript);

    // KEY_CONFIRM with server signature (ML-DSA-87)
    KeyConfirm kc;
    kc.session_id = session_id_;
    kc.transcript_hash = transcript_hash_;
    kc.timestamp = now_ms();

    // msg_to_sign = session_id || transcript_hash || timestamp(8)
    std::vector<uint8_t> msg_to_sign;
    msg_to_sign.reserve(session_id_.size() + transcript_hash_.size() + 8);
    msg_to_sign.insert(msg_to_sign.end(), session_id_.begin(), session_id_.end());
    msg_to_sign.insert(msg_to_sign.end(), transcript_hash_.begin(), transcript_hash_.end());
    uint64_t ts = kc.timestamp;
    for (int i = 7; i >= 0; --i) msg_to_sign.push_back(static_cast<uint8_t>((ts >> (8*i)) & 0xFF));

    kc.signature = sign_with_server_identity(msg_to_sign);

    std::vector<uint8_t> kc_bytes = Serializer::serialize_key_confirm(kc);
    if (!send_frame(ssl_, kc_bytes)) {
        throw std::runtime_error("Failed to send KEY_CONFIRM");
    }

    // IMPORTANT: Do not send any extra compatibility frames (KEY_OK). Drone C++ does not expect it.
    state_ = ServerState::KEY_EXCHANGE_DONE;
    log_->info("KEY_EXCHANGE ok (session_key=32B, transcript_hash=64B)");
}

void HesiaServerSession::step_drone_auth() {
    if (state_ != ServerState::KEY_EXCHANGE_DONE) {
        throw std::runtime_error("Invalid state before DRONE_AUTH");
    }

    log_->info("Waiting for DRONE_AUTH");
    std::vector<uint8_t> auth_bytes;
    if (!recv_frame(ssl_, auth_bytes, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive DRONE_AUTH");
    }
    log_->info("DRONE_AUTH frame received (bytes=" + std::to_string(auth_bytes.size()) + ")");

    BlockDroneAuth auth = Serializer::deserialize_drone_auth(auth_bytes);
    log_->info("DRONE_AUTH decoded (pk=" + std::to_string(auth.drone_pubkey.size()) +
               "B, sig=" + std::to_string(auth.signature.size()) +
               "B, tee_pk=" + std::to_string(auth.tee_attestation_pubkey.size()) +
               "B, tee_sig=" + std::to_string(auth.tee_attestation_signature.size()) + "B)");

    // Strict PoP requirements
    if (auth.session_id.size() != 16) {
        throw SecurityViolation("DRONE_AUTH: session_id manquant/invalid");
    }
    if (auth.transcript_hash.size() != 64) {
        throw SecurityViolation("DRONE_AUTH: transcript_hash manquant/invalid");
    }
    if (auth.server_cert_sha256.size() != 32) {
        throw SecurityViolation("DRONE_AUTH: server_cert_sha256 manquant (PoP TLS)");
    }

    if (!ConstantTime::equals(auth.session_id, session_id_)) {
        throw SecurityViolation("DRONE_AUTH: session_id mismatch");
    }
    if (!ConstantTime::equals(auth.transcript_hash, transcript_hash_)) {
        throw SecurityViolation("DRONE_AUTH: transcript_hash mismatch");
    }
    if (!ConstantTime::equals(auth.server_cert_sha256, tls_server_cert_sha256_)) {
        throw SecurityViolation("DRONE_AUTH: server_cert_sha256 mismatch (pinning mismatch or MITM)");
    }

    // Validate chaining
    if (!ConstantTime::equals(auth.last_block_hash, last_block_hash_)) {
        throw SecurityViolation("DRONE_AUTH: last_block_hash mismatch");
    }

    if (!expected_drone_pubkey_.empty()) {
        if (!ConstantTime::equals(auth.drone_pubkey, expected_drone_pubkey_)) {
            throw SecurityViolation("DRONE_AUTH: drone_pubkey mismatch (pinned key required)");
        }
        drone_pubkey_ = expected_drone_pubkey_;
    } else {
        drone_pubkey_ = auth.drone_pubkey;
    }

    if (!expected_drone_tee_pubkey_.empty() &&
        !ConstantTime::equals(auth.tee_attestation_pubkey, expected_drone_tee_pubkey_)) {
        throw SecurityViolation("DRONE_AUTH: TEE attestation public key mismatch");
    }

    const std::vector<uint8_t> payload = build_signed_drone_auth_payload(auth);
    try {
        if (!Dilithium::verify(drone_pubkey_, payload, auth.signature)) {
            throw InvalidSignature("DRONE_AUTH: signature invalide");
        }
    } catch (const std::exception& e) {
        throw InvalidSignature("DRONE_AUTH: verification error: " + std::string(e.what()) +
                               " (pk_len=" + std::to_string(drone_pubkey_.size()) +
                               ", payload_len=" + std::to_string(payload.size()) +
                               ", sig_len=" + std::to_string(auth.signature.size()) + ")");
    }
    if (policy_.require_tee_attestation) {
        const std::vector<uint8_t>& tee_pubkey = expected_drone_tee_pubkey_.empty()
            ? auth.tee_attestation_pubkey
            : expected_drone_tee_pubkey_;
        if (tee_pubkey.empty()) {
            throw SecurityViolation("DRONE_AUTH: TEE attestation public key missing");
        }
        const std::vector<uint8_t> tee_digest = sha256_bytes(payload);
        const bool allow_legacy_p256 = !(policy_.prod_fuse && policy_.require_tee_attestation);
        if (!allow_legacy_p256 &&
            is_legacy_p256_attestation_material(tee_pubkey, auth.tee_attestation_signature)) {
            throw SecurityViolation("DRONE_AUTH: legacy P-256 TEE attestation disabled in production");
        }
        log_->info("Verifying TEE attestation (pub=" + std::to_string(tee_pubkey.size()) +
                   "B, digest=" + std::to_string(tee_digest.size()) +
                   "B, sig=" + std::to_string(auth.tee_attestation_signature.size()) + "B)");
        if (!verify_tee_attestation_signature(tee_pubkey,
                                              tee_digest,
                                              auth.tee_attestation_signature,
                                              allow_legacy_p256)) {
            if (audit_) {
                audit_->event("TEE_ATTEST_FAIL", "ERROR", "client=" + client_label_);
            }
            throw InvalidSignature("DRONE_AUTH: TEE attestation signature invalide");
        }
        if (audit_) {
            audit_->event("TEE_ATTEST_OK", "INFO", "client=" + client_label_);
        }
    }

    // Remote attestation: firmware allowlist (policy-controlled, fail-closed)
    std::string allow_path;
    const std::filesystem::path allowlist_signing_pub =
        resolve_allowlist_signing_pubkey(secure_dir_, keys_dir_, policy_.prod_fuse);
    {
        const std::filesystem::path secure_allow = std::filesystem::path(secure_dir_) / "firmware_allowlist.txt";
        const std::filesystem::path keys_allow = std::filesystem::path(keys_dir_) / "firmware_allowlist.txt";
        if (std::filesystem::exists(secure_allow)) {
            allow_path = secure_allow.string();
        } else if (!policy_.prod_fuse && std::filesystem::exists(keys_allow)) {
            allow_path = keys_allow.string();
        } else {
            allow_path = secure_allow.string();
        }
    }
    const bool require_attest = policy_.require_attestation;
    if (require_attest) {
        auto allow = load_signed_hex_list_or_throw(allow_path, 64, policy_.prod_fuse, allowlist_signing_pub);
        if (allow.empty()) {
            throw SecurityViolation("Attestation required but allowlist missing/empty");
        }
        const std::string fw_hex = bytes_to_hex(auth.firmware_hash);
        if (allow.find(fw_hex) == allow.end()) {
            if (audit_) {
                audit_->event("ATTEST_FAIL", "ERROR", "client=" + client_label_);
            }
            throw SecurityViolation("Firmware hash not in allowlist");
        }
        if (audit_) {
            audit_->event("ATTEST_OK", "INFO", "client=" + client_label_ + " fw=" + hex_prefix(auth.firmware_hash, 16));
        }
    }

    if (policy_.require_boot_measure_allowlist) {
        std::string boot_allow_path;
        const std::filesystem::path secure_boot_allow = std::filesystem::path(secure_dir_) / "boot_measure_allowlist.txt";
        const std::filesystem::path keys_boot_allow = std::filesystem::path(keys_dir_) / "boot_measure_allowlist.txt";
        if (std::filesystem::exists(secure_boot_allow)) {
            boot_allow_path = secure_boot_allow.string();
        } else if (!policy_.prod_fuse && std::filesystem::exists(keys_boot_allow)) {
            boot_allow_path = keys_boot_allow.string();
        } else {
            boot_allow_path = secure_boot_allow.string();
        }

        auto boot_allow = load_signed_hex_list_or_throw(boot_allow_path, 64, policy_.prod_fuse, allowlist_signing_pub);
        if (boot_allow.empty()) {
            throw SecurityViolation("Measured boot required but boot_measure_allowlist missing/empty");
        }
        const std::string boot_hex = bytes_to_hex(auth.boot_measure_digest);
        if (boot_allow.find(boot_hex) == boot_allow.end()) {
            if (audit_) {
                audit_->event("BOOT_MEASURE_FAIL", "ERROR", "client=" + client_label_);
            }
            throw SecurityViolation("Measured boot digest not in allowlist");
        }
        if (audit_) {
            audit_->event("BOOT_MEASURE_OK", "INFO", "client=" + client_label_ + " boot=" + hex_prefix(auth.boot_measure_digest, 16));
        }
    }

    // Revocation list (SHA-256 of drone public key)
    std::string revoke_path;
    {
        const std::filesystem::path secure_revoke = std::filesystem::path(secure_dir_) / "revoked_drones.txt";
        const std::filesystem::path keys_revoke = std::filesystem::path(keys_dir_) / "revoked_drones.txt";
        if (std::filesystem::exists(secure_revoke)) {
            revoke_path = secure_revoke.string();
        } else if (!policy_.prod_fuse && std::filesystem::exists(keys_revoke)) {
            revoke_path = keys_revoke.string();
        } else {
            revoke_path = secure_revoke.string();
        }
    }
    if (std::filesystem::exists(revoke_path)) {
        auto revoked = load_signed_hex_list_or_throw(revoke_path, 32, policy_.prod_fuse, allowlist_signing_pub);
        if (!revoked.empty()) {
            const std::string fp_hex = bytes_to_hex(sha256_bytes(drone_pubkey_));
            if (revoked.find(fp_hex) != revoked.end()) {
                if (audit_) {
                    audit_->event("REVOKED_DRONE", "ERROR", "client=" + client_label_);
                }
                throw SecurityViolation("Drone key revoked");
            }
        }
    }

    if (audit_) {
        audit_->event("DRONE_AUTH_OK", "INFO", "client=" + client_label_);
    }
    log_->info("DRONE_AUTH payload verified");

    // Update chain (server mirrors drone behavior)
    last_block_hash_ = hash_data(payload);

    // Build SERVER_AUTH (aligned with drone::handle_server_auth)
    BlockServerAuth sa;
    sa.server_id = "HESIA_SERVER";
    sa.server_pubkey = server_pk_;
    sa.mission_id = "MISSION_ALPHA";
    sa.policy_hash = compute_policy_hash_or_throw(policy_);
    sa.last_block_hash = last_block_hash_;

    const std::vector<uint8_t> sa_payload = build_signed_server_auth_payload(sa);
    sa.signature = sign_with_server_identity(sa_payload);
    log_->info("SERVER_AUTH payload signed");

    std::vector<uint8_t> sa_bytes = Serializer::serialize_server_auth(sa);
    if (!send_frame(ssl_, sa_bytes)) {
        throw std::runtime_error("Failed to send SERVER_AUTH");
    }
    log_->info("SERVER_AUTH frame sent");

    // Update chain (must match drone)
    last_block_hash_ = hash_data(sa_payload);

    // Initialize channels
    log_->info("Initializing SecureChannel");
    secure_channel_ = std::make_unique<SecureChannel>(session_key_, SecureChannelRole::ServerResponder);
    log_->info("SecureChannel ready");
    // Video key derivation should match drone VideoChannel derivation (HKDF-SHA3-512 info).
    {
        EVP_PKEY_CTX* kdf = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!kdf) throw std::runtime_error("HKDF ctx failed");
        if (EVP_PKEY_derive_init(kdf) <= 0) { EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF init failed"); }
        if (EVP_PKEY_CTX_set_hkdf_md(kdf, EVP_sha3_512()) <= 0) { EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF md failed"); }
        // No salt for video key
        if (EVP_PKEY_CTX_set1_hkdf_key(kdf, session_key_.data(), static_cast<int>(session_key_.size())) <= 0) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF key failed");
        }
        const std::string info = "HESIA_VIDEO_STREAM_v1";
        if (EVP_PKEY_CTX_add1_hkdf_info(kdf,
            reinterpret_cast<const unsigned char*>(info.data()), static_cast<int>(info.size())) <= 0) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF info failed");
        }
        std::vector<uint8_t> video_key(32);
        size_t out_len = video_key.size();
        if (EVP_PKEY_derive(kdf, video_key.data(), &out_len) <= 0 || out_len != video_key.size()) {
            EVP_PKEY_CTX_free(kdf); throw std::runtime_error("HKDF derive failed");
        }
        EVP_PKEY_CTX_free(kdf);
        log_->info("Initializing VideoChannel (key_len=" + std::to_string(video_key.size()) + ")");
        video_channel_ = std::make_unique<VideoChannel>(video_key, 1);
        log_->info("VideoChannel ready");
    }

    state_ = ServerState::SERVER_AUTH_SENT;
    log_->info("DRONE_AUTH ok, SERVER_AUTH sent");
}

void HesiaServerSession::step_confirm() {
    if (state_ != ServerState::SERVER_AUTH_SENT) {
        throw std::runtime_error("Invalid state before CONFIRM");
    }

    log_->info("Waiting for CONFIRM");
    std::vector<uint8_t> sig;
    if (!recv_frame(ssl_, sig, policy_.max_control_msg_bytes)) {
        throw std::runtime_error("Failed to receive CONFIRM signature");
    }
    log_->info("CONFIRM received (sig_len=" + std::to_string(sig.size()) + ")");

    // confirm payload = "OK" || last_block_hash
    std::vector<uint8_t> payload;
    payload.reserve(2 + last_block_hash_.size());
    payload.push_back('O');
    payload.push_back('K');
    payload.insert(payload.end(), last_block_hash_.begin(), last_block_hash_.end());

    // Verify with cached drone public key from DRONE_AUTH
    if (drone_pubkey_.empty()) {
        throw std::runtime_error("CONFIRM: drone public key missing");
    }

    bool confirm_valid = false;
    try {
        confirm_valid = Dilithium::verify(drone_pubkey_, payload, sig);
    } catch (const std::exception& e) {
        throw InvalidSignature("CONFIRM: verification error: " + std::string(e.what()) +
                               " (sig_len=" + std::to_string(sig.size()) +
                               ", pk_len=" + std::to_string(drone_pubkey_.size()) + ")");
    }

    if (!confirm_valid) {
        throw InvalidSignature("CONFIRM: signature invalide");
    }

    // Send OK bytes (framed) as expected by drone
    std::vector<uint8_t> ok = {'O','K'};
    if (!send_frame(ssl_, ok)) {
        throw std::runtime_error("Failed to send CONFIRM OK");
    }

    state_ = ServerState::SECURE_SESSION;
    log_->info("SECURE_SESSION established");
    if (audit_) {
        audit_->event("SECURE_SESSION", "INFO", "client=" + client_label_);
    }
}

void HesiaServerSession::secure_loop() {
    if (state_ != ServerState::SECURE_SESSION) {
        return;
    }

    const std::filesystem::path ui_dir = policy_.ui_dir;

    // Receive loop
    while (true) {
        std::vector<uint8_t> payload;
        if (!recv_frame(ssl_, payload, policy_.max_frame_bytes)) {
            log_->info("Client disconnected");
            return;
        }
        if (payload.empty()) continue;

        if (rate_limiter_ && !rate_limiter_->consume(payload.size())) {
            log_->warning("Rate limit exceeded, closing session");
            if (audit_) {
                audit_->event("RATE_LIMIT", "WARN", "client=" + client_label_);
            }
            return;
        }

        const uint8_t type = payload[0];
        if (type == 0x01) {
            // SECURE_MSG: iv(12) || tag(16) || ciphertext(var)
            std::vector<uint8_t> blob(payload.begin() + 1, payload.end());
            if (blob.size() < 12 + 16) {
                log_->warning("SECURE_MSG too small");
                continue;
            }
            std::vector<uint8_t> iv(blob.begin(), blob.begin() + 12);
            std::vector<uint8_t> tag(blob.begin() + 12, blob.begin() + 12 + 16);
            std::vector<uint8_t> ct(blob.begin() + 12 + 16, blob.end());

            try {
                std::vector<uint8_t> pt = secure_channel_->decrypt(iv, ct, tag, last_block_hash_);
                
                // Sauvegarder le message déchiffré pour analyse
                if (forensic_capture_enabled(policy_, "HESIA_FORENSIC_MESSAGE_CAPTURE")) {
                    save_decrypted_frame(pt, 0, client_label_ + "_secure_msg", policy_.forensic_dir);
                }
                
                // Update chaining: SHA3-512(last_block_hash || plaintext)
                std::vector<uint8_t> chain;
                chain.reserve(last_block_hash_.size() + pt.size());
                chain.insert(chain.end(), last_block_hash_.begin(), last_block_hash_.end());
                chain.insert(chain.end(), pt.begin(), pt.end());
                last_block_hash_ = hash_data(chain);

                log_->info("SECURE_MSG ok (pt_len=" + std::to_string(pt.size()) + ")");
                if (!ui_dir.empty()) {
                    std::string pt_str(pt.begin(), pt.end());
                    if (pt_str.find("\"type\":\"TELEMETRY\"") != std::string::npos) {
                        write_telemetry_json(ui_dir, client_label_, pt_str);
                        log_->info("CONST telemetry update ok");
                    }
                }
            } catch (const std::exception& e) {
                log_->error(std::string("SECURE_MSG decrypt failed: ") + e.what());
                if (audit_) {
                    audit_->event("SECURE_MSG_FAIL", "ERROR", "client=" + client_label_);
                }
                return;
            }
        } else if (type == 0x02) {
            // VIDEO_DATA: VideoPacket blob (encrypted)
            std::vector<uint8_t> blob(payload.begin() + 1, payload.end());
            try {
                VideoPacket pkt = VideoPacket::deserialize(blob);
                std::vector<uint8_t> frame = video_channel_->decrypt_frame(pkt);
                const uint64_t frame_ts_ms = now_ms();

                if (frame.size() > policy_.max_frame_bytes) {
                    log_->warning("VIDEO_DATA too large after decrypt: " + std::to_string(frame.size()));
                    if (audit_) {
                        audit_->event("VIDEO_OVERSIZE", "WARN", "client=" + client_label_);
                    }
                    continue;
                }
                
                // Sauvegarder la frame dechiffree pour analyse
                if (forensic_capture_enabled(policy_, "HESIA_FORENSIC_VIDEO_CAPTURE")) {
                    save_decrypted_frame(frame, pkt.frame_id, client_label_, policy_.forensic_dir);
                }
                if (!ui_dir.empty() && interval_elapsed(frame_ts_ms, last_ui_video_write_ms_, 250)) {
                    write_binary_atomic(ui_dir / "latest.jpg", frame);
                    std::ostringstream meta;
                    meta << "{"
                         << "\"frame_id\":" << pkt.frame_id
                         << ",\"bytes\":" << frame.size()
                         << ",\"ts_ms\":" << frame_ts_ms
                         << "}";
                    write_text_atomic(ui_dir / "frame_meta.json", meta.str());
                }

                ++video_frames_ok_;
                if (video_frames_ok_ <= 5 || interval_elapsed(frame_ts_ms, last_video_log_ms_, 1000)) {
                    log_->info("VIDEO_DATA ok (frame_id=" + std::to_string(pkt.frame_id) +
                               ", bytes=" + std::to_string(frame.size()) +
                               ", total=" + std::to_string(video_frames_ok_) + ")");
                }
            } catch (const std::exception& e) {
                log_->error(std::string("VIDEO_DATA handling failed: ") + e.what());
                if (audit_) {
                    audit_->event("VIDEO_FAIL", "ERROR", "client=" + client_label_);
                }
                return;
            }
        } else {
            log_->warning("Unknown payload type: " + std::to_string(type));
            if (audit_) {
                audit_->event("PROTO_ERROR", "WARN", "client=" + client_label_);
            }
            return;
        }
    }
}

} // namespace hesia
