#include "policy.hpp"
#include "crypto_real.hpp"
#include "policy_ed25519_public_key.h"
#include "policy_pqc_public_key.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>

namespace hesia {

namespace {

#ifndef HESIA_POLICY_DIR
#define HESIA_POLICY_DIR "/etc/hesia/policy"
#endif

constexpr int kPolicySchemaVersion = 1;

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

static bool to_bool(const std::string& v, bool defv) {
    std::string t = v;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (t == "1" || t == "true" || t == "yes" || t == "on") return true;
    if (t == "0" || t == "false" || t == "no" || t == "off") return false;
    return defv;
}

static std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::vector<uint8_t> base64_decode(const std::string& b64) {
    std::string compact;
    compact.reserve(b64.size());
    for (char c : b64) {
        if (!std::isspace(static_cast<unsigned char>(c))) compact.push_back(c);
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

static bool verify_ed25519_signature(const std::vector<uint8_t>& data,
                                     const std::vector<uint8_t>& sig,
                                     const std::string& pubkey_pem) {
    if (sig.size() != 64) {
        return false;
    }
    if (pubkey_pem.empty()) {
        throw std::runtime_error("Embedded policy Ed25519 public key is empty");
    }
    BIO* bio = BIO_new_mem_buf(pubkey_pem.data(),
                               static_cast<int>(pubkey_pem.size()));
    if (!bio) {
        throw std::runtime_error("BIO_new_mem_buf failed");
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        throw std::runtime_error("Failed to read embedded policy public key");
    }
    if (EVP_PKEY_base_id(pkey) != EVP_PKEY_ED25519) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Embedded policy public key is not Ed25519");
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
    ok = EVP_DigestVerify(ctx,
                          sig.data(),
                          sig.size(),
                          data.data(),
                          data.size());
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok == 1;
}

static bool verify_dilithium_signature(const std::vector<uint8_t>& data,
                                       const std::vector<uint8_t>& sig,
                                       const std::vector<uint8_t>& pubkey) {
#if defined(HAVE_LIBOQS) && !defined(HESIA_FIPS_BUILD)
    if (sig.empty() || pubkey.empty()) {
        return false;
    }
    return Dilithium::verify(pubkey, data, sig);
#else
    (void)data;
    (void)sig;
    (void)pubkey;
    throw std::runtime_error("PQC policy verification requires liboqs and non-FIPS build");
#endif
}

static std::vector<uint8_t> get_embedded_policy_pqc_pubkey() {
    if (kPolicyPqcPublicKeyLen == 0) {
        throw std::runtime_error("Embedded policy PQC public key is empty");
    }
    return std::vector<uint8_t>(kPolicyPqcPublicKey,
                                kPolicyPqcPublicKey + kPolicyPqcPublicKeyLen);
}

static std::string get_embedded_policy_ed25519_pubkey_pem() {
    if (kPolicyEd25519PublicKeyPemLen == 0) {
        throw std::runtime_error("Embedded policy Ed25519 public key is empty");
    }
    return std::string(reinterpret_cast<const char*>(kPolicyEd25519PublicKeyPem),
                       kPolicyEd25519PublicKeyPemLen);
}

static std::unordered_map<std::string, std::string> parse_kv(const std::string& content) {
    std::unordered_map<std::string, std::string> out;
    std::istringstream iss(content);
    std::string line;
    size_t line_no = 0;
    while (std::getline(iss, line)) {
        ++line_no;
        std::string t = trim_copy(line);
        if (t.empty() || t[0] == '#') continue;
        auto pos = t.find('=');
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid security policy line " + std::to_string(line_no) +
                                     ": missing '='");
        }
        std::string key = trim_copy(t.substr(0, pos));
        std::string val = trim_copy(t.substr(pos + 1));
        if (key.empty()) {
            throw std::runtime_error("Invalid security policy line " + std::to_string(line_no) +
                                     ": empty key");
        }
        if (!out.emplace(key, val).second) {
            throw std::runtime_error("Duplicate security policy key: " + key);
        }
    }
    return out;
}

static const std::unordered_set<std::string>& allowed_policy_keys() {
    static const std::unordered_set<std::string> allowed = []() {
        static constexpr std::array<const char*, 58> suffixes = {
            "version",
            "require_mtls",
            "require_attestation",
            "prod_fuse",
            "incident_mode",
            "require_exporter_binding",
            "require_tee_hkdf",
            "require_tee_attestation",
            "require_optee_session_auth",
            "require_mldsa_sign_in_tee",
            "allow_ephemeral_dilithium",
            "allow_ephemeral_puf",
            "ssl_read_timeout_sec",
            "ssl_write_timeout_sec",
            "server_host",
            "server_port",
            "max_control_msg_bytes",
            "max_frame_bytes",
            "video_send_queue_max",
            "video_min_send_interval_ms",
            "rate_limit_bps",
            "rate_limit_burst",
            "tls_pin",
            "tls_rekey_bytes",
            "tls_rekey_seconds",
            "secure_dir",
            "sealed_puf_path",
            "sealed_dilithium_path",
            "optee_session_auth_path",
            "require_oem_kdf",
            "oem_k1_path",
            "oem_k2_path",
            "oem_kdf_label",
            "oem_kdf_context",
            "firmware_version",
            "firmware_version_file",
            "require_boot_measure",
            "boot_measure_path",
            "boot_measure_sig_path",
            "boot_measure_pubkey_path",
            "boot_measure_format",
            "boot_measure_expected_source",
            "boot_measure_max_age_sec",
            "require_asset_manifest",
            "asset_manifest_path",
            "asset_manifest_sig_path",
            "asset_manifest_pubkey_path",
            "asset_manifest_format",
            "asset_manifest_expected_slot",
            "asset_manifest_max_age_sec",
            "require_ab_slots",
            "require_rpmb_rollback_storage",
            "require_release_signature",
            "release_target_path",
            "release_sig_path",
            "release_pubkey_path",
            "audit_enabled",
            "audit_log_path"
        };
        static constexpr std::array<const char*, 9> more_suffixes = {
            "audit_key_path",
            "audit_alert_path",
            "audit_signing_key",
            "audit_signing_pub",
            "audit_export_path",
            "require_audit_signing",
            "audit_rotate_on_start",
            "audit_rotate_interval_sec",
            "cert_dir"
        };
        static constexpr std::array<const char*, 14> final_suffixes = {
            "client_cert",
            "client_key",
            "ca_cert",
            "bind_addr",
            "port",
            "server_cert",
            "server_key",
            "keys_dir",
            "drone_pubkey_file",
            "drone_tee_pubkey_file",
            "require_pinned_drone_pubkey",
            "forensic_dir",
            "ui_dir",
            "require_boot_measure_allowlist"
        };
        static constexpr std::array<const char*, 4> server_limits = {
            "max_conn_total",
            "max_conn_per_ip",
            "max_pending_queue",
            "worker_threads"
        };

        std::unordered_set<std::string> out;
        out.insert("schema_version");
        for (const char* suffix : suffixes) {
            out.insert(suffix);
            out.insert(std::string("drone.") + suffix);
            out.insert(std::string("server.") + suffix);
        }
        for (const char* suffix : more_suffixes) {
            out.insert(suffix);
            out.insert(std::string("drone.") + suffix);
            out.insert(std::string("server.") + suffix);
        }
        for (const char* suffix : final_suffixes) {
            out.insert(suffix);
            out.insert(std::string("drone.") + suffix);
            out.insert(std::string("server.") + suffix);
        }
        for (const char* suffix : server_limits) {
            out.insert(suffix);
            out.insert(std::string("server.") + suffix);
        }
        return out;
    }();
    return allowed;
}

static void validate_policy_kv_or_throw(const std::unordered_map<std::string, std::string>& kv) {
    const auto& allowed = allowed_policy_keys();
    for (const auto& [key, _] : kv) {
        if (allowed.find(key) == allowed.end()) {
            throw std::runtime_error("Unsupported security policy key: " + key);
        }
    }

    const auto schema_it = kv.find("schema_version");
    if (schema_it == kv.end()) {
        throw std::runtime_error("Security policy schema_version missing");
    }

    int schema_version = 0;
    try {
        schema_version = std::stoi(schema_it->second);
    } catch (...) {
        throw std::runtime_error("Invalid security policy schema_version: " + schema_it->second);
    }

    if (schema_version != kPolicySchemaVersion) {
        throw std::runtime_error("Unsupported security policy schema_version: " +
                                 std::to_string(schema_version));
    }
}

static std::string get_key(const std::unordered_map<std::string, std::string>& kv,
                           const std::string& role,
                           const std::string& key,
                           const std::string& defv) {
    const std::string role_key = role + "." + key;
    auto it = kv.find(role_key);
    if (it != kv.end()) return it->second;
    it = kv.find(key);
    if (it != kv.end()) return it->second;
    return defv;
}

static int get_int(const std::unordered_map<std::string, std::string>& kv,
                   const std::string& role,
                   const std::string& key,
                   int defv) {
    try {
        return std::stoi(get_key(kv, role, key, std::to_string(defv)));
    } catch (...) {
        return defv;
    }
}

static std::uint64_t get_u64(const std::unordered_map<std::string, std::string>& kv,
                             const std::string& role,
                             const std::string& key,
                             std::uint64_t defv) {
    try {
        return static_cast<std::uint64_t>(std::stoull(get_key(kv, role, key, std::to_string(defv))));
    } catch (...) {
        return defv;
    }
}

static std::size_t get_size(const std::unordered_map<std::string, std::string>& kv,
                            const std::string& role,
                            const std::string& key,
                            std::size_t defv) {
    try {
        return static_cast<std::size_t>(std::stoull(get_key(kv, role, key, std::to_string(defv))));
    } catch (...) {
        return defv;
    }
}

static bool get_bool(const std::unordered_map<std::string, std::string>& kv,
                     const std::string& role,
                     const std::string& key,
                     bool defv) {
    return to_bool(get_key(kv, role, key, defv ? "1" : "0"), defv);
}

} // namespace

std::string resolve_path(const std::string& base_dir, const std::string& value) {
    if (value.empty()) return value;
    std::filesystem::path p(value);
    if (p.is_absolute()) return value;
    return (std::filesystem::path(base_dir) / p).string();
}

SecurityPolicy load_security_policy_or_throw(const std::string& role) {
    SecurityPolicy p;
    p.policy_dir = HESIA_POLICY_DIR;
    p.policy_path = (std::filesystem::path(p.policy_dir) / "policy.conf").string();
    p.policy_sig_path = (std::filesystem::path(p.policy_dir) / "policy.sig").string();
    p.policy_pubkey_path = (std::filesystem::path(p.policy_dir) / "policy_pub.pem").string();
    p.policy_sig_pqc_path = (std::filesystem::path(p.policy_dir) / "policy.sig.pqc").string();
    p.policy_pubkey_pqc_path = (std::filesystem::path(p.policy_dir) / "policy_pub.pqc").string();

    if (!std::filesystem::exists(p.policy_path) ||
        !std::filesystem::exists(p.policy_sig_path)) {
        throw std::runtime_error("Security policy files missing in " + p.policy_dir);
    }

    const std::string policy_text = read_text_file(p.policy_path);
    const std::string sig_text = read_text_file(p.policy_sig_path);
    std::vector<uint8_t> sig = base64_decode(sig_text);
    std::vector<uint8_t> data(policy_text.begin(), policy_text.end());

    const std::string ed25519_pub_pem = get_embedded_policy_ed25519_pubkey_pem();
    if (!verify_ed25519_signature(data, sig, ed25519_pub_pem)) {
        throw std::runtime_error("Security policy signature invalid");
    }

#if defined(HAVE_LIBOQS) && !defined(HESIA_FIPS_BUILD)
    if (!std::filesystem::exists(p.policy_sig_pqc_path)) {
        throw std::runtime_error("Security policy PQC files missing in " + p.policy_dir);
    }
    const std::string sig_pqc_text = read_text_file(p.policy_sig_pqc_path);
    std::vector<uint8_t> sig_pqc = base64_decode(sig_pqc_text);
    std::vector<uint8_t> pub_pqc = get_embedded_policy_pqc_pubkey();
    if (!verify_dilithium_signature(data, sig_pqc, pub_pqc)) {
        throw std::runtime_error("Security policy PQC signature invalid");
    }
#endif

    auto kv = parse_kv(policy_text);
    validate_policy_kv_or_throw(kv);

    p.schema_version = get_int(kv, role, "schema_version", p.schema_version);

    p.version = get_int(kv, role, "version", p.version);
    p.require_mtls = get_bool(kv, role, "require_mtls", p.require_mtls);
    p.require_attestation = get_bool(kv, role, "require_attestation", p.require_attestation);
    p.prod_fuse = get_bool(kv, role, "prod_fuse", p.prod_fuse);
    p.incident_mode = get_bool(kv, role, "incident_mode", p.incident_mode);
    p.require_exporter_binding = get_bool(kv, role, "require_exporter_binding", p.require_exporter_binding);
    p.require_tee_hkdf = get_bool(kv, role, "require_tee_hkdf", p.require_tee_hkdf);
    p.require_tee_attestation = get_bool(kv, role, "require_tee_attestation", p.require_tee_attestation);
    p.require_optee_session_auth = get_bool(kv, role, "require_optee_session_auth", p.require_optee_session_auth);
    p.require_mldsa_sign_in_tee = get_bool(kv, role, "require_mldsa_sign_in_tee", p.require_mldsa_sign_in_tee);
    p.allow_ephemeral_dilithium = get_bool(kv, role, "allow_ephemeral_dilithium", p.allow_ephemeral_dilithium);
    p.allow_ephemeral_puf = get_bool(kv, role, "allow_ephemeral_puf", p.allow_ephemeral_puf);

    p.ssl_read_timeout_sec = get_int(kv, role, "ssl_read_timeout_sec", p.ssl_read_timeout_sec);
    p.ssl_write_timeout_sec = get_int(kv, role, "ssl_write_timeout_sec", p.ssl_write_timeout_sec);

    p.server_host = get_key(kv, role, "server_host", p.server_host);
    p.server_port = get_int(kv, role, "server_port", p.server_port);

    p.max_control_msg_bytes = get_size(kv, role, "max_control_msg_bytes", p.max_control_msg_bytes);
    p.max_frame_bytes = get_size(kv, role, "max_frame_bytes", p.max_frame_bytes);
    p.video_send_queue_max = get_size(kv, role, "video_send_queue_max", p.video_send_queue_max);
    p.video_min_send_interval_ms = get_u64(kv, role, "video_min_send_interval_ms", p.video_min_send_interval_ms);

    p.rate_limit_bps = get_u64(kv, role, "rate_limit_bps", p.rate_limit_bps);
    p.rate_limit_burst = get_u64(kv, role, "rate_limit_burst", p.rate_limit_burst);

    p.tls_pin = get_bool(kv, role, "tls_pin", p.tls_pin);
    p.tls_rekey_bytes = get_u64(kv, role, "tls_rekey_bytes", p.tls_rekey_bytes);
    p.tls_rekey_seconds = get_int(kv, role, "tls_rekey_seconds", p.tls_rekey_seconds);

    p.secure_dir = get_key(kv, role, "secure_dir", p.secure_dir);
    p.sealed_puf_path = get_key(kv, role, "sealed_puf_path", p.sealed_puf_path);
    p.sealed_dilithium_path = get_key(kv, role, "sealed_dilithium_path", p.sealed_dilithium_path);
    p.optee_session_auth_path = get_key(kv, role, "optee_session_auth_path", p.optee_session_auth_path);

    p.require_oem_kdf = get_bool(kv, role, "require_oem_kdf", p.require_oem_kdf);
    p.oem_k1_path = get_key(kv, role, "oem_k1_path", p.oem_k1_path);
    p.oem_k2_path = get_key(kv, role, "oem_k2_path", p.oem_k2_path);
    p.oem_kdf_label = get_key(kv, role, "oem_kdf_label", p.oem_kdf_label);
    p.oem_kdf_context = get_key(kv, role, "oem_kdf_context", p.oem_kdf_context);

    p.firmware_version = get_u64(kv, role, "firmware_version", p.firmware_version);
    p.firmware_version_file = get_key(kv, role, "firmware_version_file", p.firmware_version_file);
    p.require_boot_measure = get_bool(kv, role, "require_boot_measure", p.require_boot_measure);
    p.boot_measure_path = get_key(kv, role, "boot_measure_path", p.boot_measure_path);
    p.boot_measure_sig_path = get_key(kv, role, "boot_measure_sig_path", p.boot_measure_sig_path);
    p.boot_measure_pubkey_path = get_key(kv, role, "boot_measure_pubkey_path", p.boot_measure_pubkey_path);
    p.boot_measure_format = get_key(kv, role, "boot_measure_format", p.boot_measure_format);
    p.boot_measure_expected_source = get_key(kv, role, "boot_measure_expected_source", p.boot_measure_expected_source);
    p.boot_measure_max_age_sec = get_u64(kv, role, "boot_measure_max_age_sec", p.boot_measure_max_age_sec);
    p.require_asset_manifest = get_bool(kv, role, "require_asset_manifest", p.require_asset_manifest);
    p.asset_manifest_path = get_key(kv, role, "asset_manifest_path", p.asset_manifest_path);
    p.asset_manifest_sig_path = get_key(kv, role, "asset_manifest_sig_path", p.asset_manifest_sig_path);
    p.asset_manifest_pubkey_path = get_key(kv, role, "asset_manifest_pubkey_path", p.asset_manifest_pubkey_path);
    p.asset_manifest_format = get_key(kv, role, "asset_manifest_format", p.asset_manifest_format);
    p.asset_manifest_expected_slot = get_key(kv, role, "asset_manifest_expected_slot", p.asset_manifest_expected_slot);
    p.asset_manifest_max_age_sec = get_u64(kv, role, "asset_manifest_max_age_sec", p.asset_manifest_max_age_sec);
    p.require_ab_slots = get_bool(kv, role, "require_ab_slots", p.require_ab_slots);
    p.require_rpmb_rollback_storage = get_bool(kv, role, "require_rpmb_rollback_storage", p.require_rpmb_rollback_storage);

    p.require_release_signature = get_bool(kv, role, "require_release_signature", p.require_release_signature);
    p.release_target_path = get_key(kv, role, "release_target_path", p.release_target_path);
    p.release_sig_path = get_key(kv, role, "release_sig_path", p.release_sig_path);
    p.release_pubkey_path = get_key(kv, role, "release_pubkey_path", p.release_pubkey_path);

    p.audit_enabled = get_bool(kv, role, "audit_enabled", p.audit_enabled);
    p.audit_log_path = get_key(kv, role, "audit_log_path", p.audit_log_path);
    p.audit_key_path = get_key(kv, role, "audit_key_path", p.audit_key_path);
    p.audit_alert_path = get_key(kv, role, "audit_alert_path", p.audit_alert_path);
    p.audit_signing_key = get_key(kv, role, "audit_signing_key", p.audit_signing_key);
    p.audit_signing_pub = get_key(kv, role, "audit_signing_pub", p.audit_signing_pub);
    p.audit_export_path = get_key(kv, role, "audit_export_path", p.audit_export_path);
    p.require_audit_signing = get_bool(kv, role, "require_audit_signing", p.require_audit_signing);
    p.audit_rotate_on_start = get_bool(kv, role, "audit_rotate_on_start", p.audit_rotate_on_start);
    p.audit_rotate_interval_sec = get_u64(kv, role, "audit_rotate_interval_sec", p.audit_rotate_interval_sec);

    p.cert_dir = get_key(kv, role, "cert_dir", p.cert_dir);
    p.client_cert = get_key(kv, role, "client_cert", p.client_cert);
    p.client_key = get_key(kv, role, "client_key", p.client_key);
    p.ca_cert = get_key(kv, role, "ca_cert", p.ca_cert);

    return p;
}

} // namespace hesia
