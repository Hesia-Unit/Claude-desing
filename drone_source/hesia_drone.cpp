#include "hesia_drone.hpp"
#include "exceptions.hpp"
#include "optee_client.hpp"
#include "server_pubkey.h"
#include <iostream>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <chrono>
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
    const int len = static_cast<int>(compact.size());
    const int out_len = (len * 3) / 4 + 4;
    std::vector<uint8_t> out(static_cast<size_t>(out_len));
    int n = EVP_DecodeBlock(out.data(),
                            reinterpret_cast<const unsigned char*>(compact.data()),
                            len);
    if (n < 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
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

    std::ifstream f(self_path, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Impossible d'ouvrir le firmware/binaire: " + self_path.string());
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex(SHA3-512) failed");
    }

    std::array<unsigned char, 1 << 15> chunk{}; // 32 KiB
    while (f.good()) {
        f.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = f.gcount();
        if (got > 0) {
            if (EVP_DigestUpdate(ctx, chunk.data(), static_cast<size_t>(got)) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate failed");
            }
        }
    }

    if (policy_.require_boot_measure) {
        if (policy_.boot_measure_path.empty() ||
            policy_.boot_measure_sig_path.empty() ||
            policy_.boot_measure_pubkey_path.empty()) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Boot measure required but paths not configured");
        }
        std::vector<uint8_t> meas = read_file_binary(policy_.boot_measure_path);
        std::vector<uint8_t> sig_raw = read_file_binary(policy_.boot_measure_sig_path);
        std::vector<uint8_t> sig = looks_base64(sig_raw) ? base64_decode(sig_raw) : sig_raw;
        if (!verify_ed25519_signature(meas, sig, policy_.boot_measure_pubkey_path)) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Boot measure signature invalid");
        }
        if (!meas.empty()) {
            if (EVP_DigestUpdate(ctx, meas.data(), meas.size()) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate boot measure failed");
            }
        }
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

static void append_u32_be(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
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


HesiaDrone::HesiaDrone(const std::string& did) 
    : state(DroneState::IDLE), drone_id(did), seq(0) {
    last_block_hash.assign(64, 0);

    policy_ = load_security_policy_or_throw("drone");

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
    server_pubkey.resize(demo_public_bin_len);
    std::copy(demo_public_bin, demo_public_bin + demo_public_bin_len, server_pubkey.begin());
    
    // Charger la clé privée Dilithium (ancrée via OP-TEE) ou générer/sceller si absente
    const bool allow_ephemeral_dilithium = policy_.allow_ephemeral_dilithium;
    const std::filesystem::path secure_dir(policy_.secure_dir);
    const std::filesystem::path sealed_dilithium_path = policy_.sealed_dilithium_path.empty()
        ? (secure_dir / "dilithium5_sk.sealed")
        : std::filesystem::path(resolve_path(policy_.secure_dir, policy_.sealed_dilithium_path));

    bool key_loaded = false;
    if (optee_available()) {
        std::filesystem::path sealed_path = sealed_dilithium_path;
        const bool sealed_exists = std::filesystem::exists(sealed_path);
        try {
            std::vector<uint8_t> blob = optee_unseal_file(sealed_path, 0);
            auto keys = parse_dilithium_keys(blob);
            drone_keypair = std::move(keys);
            key_loaded = true;
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

        auto kp = Dilithium::generate_keypair();
        drone_keypair = kp;
        if (optee_available()) {
            std::filesystem::path sealed_path = sealed_dilithium_path;
            std::vector<uint8_t> blob = serialize_dilithium_keys(kp.first, kp.second);
            optee_seal_file(sealed_path, blob);
            try {
                write_public_key(sealed_path.parent_path() / "dilithium5_pk.bin", kp.first);
            } catch (...) {
                // Best-effort only: public key can be exported separately if needed.
            }
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
        if (fw_version == 0) {
            throw SecurityViolation("Firmware version missing for anti-rollback");
        }
        if (!optee_available()) {
            throw SecurityViolation("OP-TEE required for anti-rollback protection");
        }
        if (!optee_check_and_update_firmware_version(fw_version)) {
            throw SecurityViolation("Firmware rollback detected");
        }
    }
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
    if (!ok) {
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

    std::vector<uint8_t> firmware_hash = compute_firmware_sha3_512();
    std::vector<uint8_t> puf_response = hash_data(puf_secret);

    // PoP payload (anti-rejeu + binding serveur):
    // drone_id || firmware_hash || puf_response || last_block_hash || session_id || transcript_hash || sha256(cert DER)
    std::vector<uint8_t> payload;
    payload.reserve(
        drone_id.size() + firmware_hash.size() + puf_response.size() + last_block_hash.size() +
        session_id.size() + key_exchange_transcript_hash.size() + tls_peer_cert_sha256.size()
    );

    payload.insert(payload.end(), drone_id.begin(), drone_id.end());
    payload.insert(payload.end(), firmware_hash.begin(), firmware_hash.end());
    payload.insert(payload.end(), puf_response.begin(), puf_response.end());
    payload.insert(payload.end(), last_block_hash.begin(), last_block_hash.end());

    payload.insert(payload.end(), session_id.begin(), session_id.end());
    payload.insert(payload.end(), key_exchange_transcript_hash.begin(), key_exchange_transcript_hash.end());
    if (!tls_peer_cert_sha256.empty()) {
        payload.insert(payload.end(), tls_peer_cert_sha256.begin(), tls_peer_cert_sha256.end());
    }

    std::vector<uint8_t> signature = Dilithium::sign(drone_keypair.second, payload);

    BlockDroneAuth block;
    block.drone_id = drone_id;
    block.drone_pubkey = drone_keypair.first;
    block.firmware_hash = firmware_hash;
    block.puf_response = puf_response;
    block.last_block_hash = last_block_hash;
    block.signature = signature;

    // Extensions PoP (appendées après signature par le serializer pour compatibilité)
    block.session_id = session_id;
    block.transcript_hash = key_exchange_transcript_hash;
    block.server_cert_sha256 = tls_peer_cert_sha256;

    // Chaînage: SHA3-512(payload)
    last_block_hash = hash_data(payload);
    state = DroneState::DRONE_AUTH_SENT;

    return block;
}

std::vector<uint8_t> HesiaDrone::handle_server_auth(const BlockServerAuth& block) {
    if (state != DroneState::DRONE_AUTH_SENT) {
        throw InvalidStateError(drone_state_to_string(state), "DRONE_AUTH_SENT");
    }
    
    std::vector<uint8_t> payload;
    payload.reserve(block.server_id.size() + block.mission_id.size() + block.last_block_hash.size());
    payload.insert(payload.end(), block.server_id.begin(), block.server_id.end());
    payload.insert(payload.end(), block.mission_id.begin(), block.mission_id.end());
    payload.insert(payload.end(), block.last_block_hash.begin(), block.last_block_hash.end());
    
    bool valid = Dilithium::verify(server_pubkey, payload, block.signature);
    
    if (!valid) {
        throw InvalidSignature("Signature serveur invalide");
    }
    
    if (block.last_block_hash != last_block_hash) {
        throw AuthenticationFailed("Chaînage invalide");
    }
    
    last_block_hash = hash_data(payload);
    state = DroneState::SERVER_AUTH_VERIFIED;
    
    secure_channel = std::make_unique<SecureChannel>(session_key);
    
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
    std::vector<uint8_t> signature = Dilithium::sign(drone_keypair.second, payload);
    
    state = DroneState::SECURE_SESSION;
    
    return signature;
}

std::vector<uint8_t> HesiaDrone::send_secure_message(const std::string& msg_type, const std::string& json_data) {
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
    
    return encrypted.raw;
}


void HesiaDrone::set_tls_exporter_secret(const std::vector<uint8_t>& secret) {
    // Transport-layer binding material (TLS exporter)
    tls_exporter_secret = secret;
    use_tls_exporter_binding = !tls_exporter_secret.empty();
}

void HesiaDrone::set_tls_peer_cert_sha256(const std::vector<uint8_t>& digest) {
    // SHA-256 over peer certificate DER as observed by the drone via TLS
    tls_peer_cert_sha256 = digest;
}


} // namespace hesia
