#include "policy.hpp"
#include "policy_ed25519_public_key.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>

namespace hesia {

namespace {

#ifndef HESIA_POLICY_DIR
#define HESIA_POLICY_DIR "/etc/hesia/policy"
#endif

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

static std::vector<uint8_t> read_binary_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
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
    if (sig.size() != 64) return false;
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
    while (std::getline(iss, line)) {
        std::string t = trim_copy(line);
        if (t.empty() || t[0] == '#') continue;
        auto pos = t.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim_copy(t.substr(0, pos));
        std::string val = trim_copy(t.substr(pos + 1));
        if (!key.empty()) {
            out[key] = val;
        }
    }
    return out;
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

    auto kv = parse_kv(policy_text);

    p.version = get_int(kv, role, "version", p.version);
    p.require_mtls = get_bool(kv, role, "require_mtls", p.require_mtls);
    p.require_attestation = get_bool(kv, role, "require_attestation", p.require_attestation);
    p.prod_fuse = get_bool(kv, role, "prod_fuse", p.prod_fuse);
    p.incident_mode = get_bool(kv, role, "incident_mode", p.incident_mode);

    p.ssl_read_timeout_sec = get_int(kv, role, "ssl_read_timeout_sec", p.ssl_read_timeout_sec);
    p.ssl_write_timeout_sec = get_int(kv, role, "ssl_write_timeout_sec", p.ssl_write_timeout_sec);

    p.max_control_msg_bytes = get_size(kv, role, "max_control_msg_bytes", p.max_control_msg_bytes);
    p.max_frame_bytes = get_size(kv, role, "max_frame_bytes", p.max_frame_bytes);

    p.max_conn_total = get_int(kv, role, "max_conn_total", p.max_conn_total);
    p.max_conn_per_ip = get_int(kv, role, "max_conn_per_ip", p.max_conn_per_ip);
    p.max_pending_queue = get_int(kv, role, "max_pending_queue", p.max_pending_queue);
    p.worker_threads = get_int(kv, role, "worker_threads", p.worker_threads);

    p.rate_limit_bps = get_u64(kv, role, "rate_limit_bps", p.rate_limit_bps);
    p.rate_limit_burst = get_u64(kv, role, "rate_limit_burst", p.rate_limit_burst);

    p.bind_addr = get_key(kv, role, "bind_addr", p.bind_addr);
    p.port = get_int(kv, role, "port", p.port);

    p.cert_dir = get_key(kv, role, "cert_dir", p.cert_dir);
    p.server_cert = get_key(kv, role, "server_cert", p.server_cert);
    p.server_key = get_key(kv, role, "server_key", p.server_key);
    p.ca_cert = get_key(kv, role, "ca_cert", p.ca_cert);
    p.keys_dir = get_key(kv, role, "keys_dir", p.keys_dir);
    p.secure_dir = get_key(kv, role, "secure_dir", p.secure_dir);
    p.drone_pubkey_file = get_key(kv, role, "drone_pubkey_file", p.drone_pubkey_file);
    p.require_pinned_drone_pubkey = get_bool(kv, role, "require_pinned_drone_pubkey", p.require_pinned_drone_pubkey);

    p.forensic_dir = get_key(kv, role, "forensic_dir", p.forensic_dir);
    p.ui_dir = get_key(kv, role, "ui_dir", p.ui_dir);

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

    p.require_release_signature = get_bool(kv, role, "require_release_signature", p.require_release_signature);
    p.release_target_path = get_key(kv, role, "release_target_path", p.release_target_path);
    p.release_sig_path = get_key(kv, role, "release_sig_path", p.release_sig_path);
    p.release_pubkey_path = get_key(kv, role, "release_pubkey_path", p.release_pubkey_path);

    return p;
}

} // namespace hesia
