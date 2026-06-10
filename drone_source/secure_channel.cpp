#include "secure_channel.hpp"

#include "security_utils.hpp"
#ifdef HESIA_USE_FIPS_MODULE
#include "fips_module.hpp"
#endif
#include "logger.hpp"
#include "config.hpp"

#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace hesia {

namespace {

constexpr size_t kAes256KeyLen = 32;
constexpr size_t kIvLen = 12;
constexpr size_t kTagLen = 16;
constexpr size_t kPrefixLen = 8;
constexpr size_t kCounterLen = kIvLen - kPrefixLen;
constexpr size_t kReplayWindowSize = 64;
constexpr size_t kMaxPayloadSize = 1024 * 1024;
constexpr uint64_t kMaxCounterPerEpoch = 0xFFFFFFFFull;

std::array<uint8_t, 32> compute_hmac_sha256_or_throw(const std::vector<uint8_t>& mac_key,
                                                     const std::vector<uint8_t>& data) {
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) {
        throw SecureChannelError("EVP_MAC_fetch(HMAC) failed for key integrity");
    }

    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!ctx) {
        throw SecureChannelError("EVP_MAC_CTX_new failed for key integrity");
    }

    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_MAC_PARAM_DIGEST,
        const_cast<char*>("SHA256"),
        0);
    params[1] = OSSL_PARAM_construct_end();

    std::array<uint8_t, 32> out{};
    size_t out_len = out.size();
    if (EVP_MAC_init(ctx, mac_key.data(), mac_key.size(), params) != 1 ||
        EVP_MAC_update(ctx, data.data(), data.size()) != 1 ||
        EVP_MAC_final(ctx, out.data(), &out_len, out.size()) != 1 ||
        out_len != out.size()) {
        EVP_MAC_CTX_free(ctx);
        throw SecureChannelError("Failed to compute key integrity HMAC");
    }

    EVP_MAC_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> derive_bytes_or_throw(const std::vector<uint8_t>& session_key,
                                           const std::string& label,
                                           size_t out_len) {
    const std::vector<uint8_t> salt{
        'h','e','s','i','a','-','s','e','c','u','r','e','-','c','h','a','n','n','e','l','-','v','3'
    };
    const std::vector<uint8_t> info(label.begin(), label.end());
    return HKDF::derive(salt, session_key, info, out_len);
}

std::string outbound_direction_label(SecureChannelRole role) {
    return role == SecureChannelRole::DroneClient
        ? "hesia-secure-channel:drone-to-server"
        : "hesia-secure-channel:server-to-drone";
}

std::string inbound_direction_label(SecureChannelRole role) {
    return role == SecureChannelRole::DroneClient
        ? "hesia-secure-channel:server-to-drone"
        : "hesia-secure-channel:drone-to-server";
}

std::vector<uint8_t> key_canary_input(const char* label, const std::vector<uint8_t>& key) {
    std::vector<uint8_t> material;
    const size_t label_len = std::strlen(label);
    material.reserve(label_len + key.size());
    material.insert(material.end(), label, label + label_len);
    material.insert(material.end(), key.begin(), key.end());
    return material;
}

bool secure_channel_debug_enabled() {
    const char* env = std::getenv("HESIA_DEBUG_SECURE_CHANNEL");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}

std::string bytes_to_hex_prefix(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", data[i]);
        out += buf;
    }
    return out;
}

std::string role_name(SecureChannelRole role) {
    return role == SecureChannelRole::DroneClient ? "drone" : "server";
}

} // namespace

std::vector<uint8_t> SecureChannel::build_iv(const std::array<uint8_t, 8>& prefix,
                                             uint32_t counter) const {
    std::vector<uint8_t> iv(kIvLen, 0);
    std::copy(prefix.begin(), prefix.end(), iv.begin());
    for (int i = 0; i < static_cast<int>(kCounterLen); ++i) {
        iv[kPrefixLen + (kCounterLen - 1 - i)] =
            static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
    }
    return iv;
}

std::vector<uint8_t> SecureChannel::build_effective_aad(const std::vector<uint8_t>& aad) const {
    std::vector<uint8_t> effective_aad;
    effective_aad.reserve(8 + aad.size());
    effective_aad.push_back(static_cast<uint8_t>('H'));
    effective_aad.push_back(static_cast<uint8_t>('S'));
    effective_aad.push_back(static_cast<uint8_t>('C'));
    effective_aad.push_back(static_cast<uint8_t>('2'));

    const uint32_t epoch = key_epoch.load(std::memory_order_relaxed);
    effective_aad.push_back(static_cast<uint8_t>((epoch >> 24) & 0xFF));
    effective_aad.push_back(static_cast<uint8_t>((epoch >> 16) & 0xFF));
    effective_aad.push_back(static_cast<uint8_t>((epoch >> 8) & 0xFF));
    effective_aad.push_back(static_cast<uint8_t>(epoch & 0xFF));
    effective_aad.insert(effective_aad.end(), aad.begin(), aad.end());
    return effective_aad;
}

void SecureChannel::refresh_key_integrity_canary() {
    const std::vector<uint8_t> tx_input = key_canary_input("tx", tx_key_);
    const std::vector<uint8_t> rx_input = key_canary_input("rx", rx_key_);
    tx_key_integrity_canary_ = compute_hmac_sha256_or_throw(key_integrity_hmac_key, tx_input);
    rx_key_integrity_canary_ = compute_hmac_sha256_or_throw(key_integrity_hmac_key, rx_input);
}

bool SecureChannel::verify_key_integrity() {
    if (!fault_detection_enabled) {
        return true;
    }
    if (tx_key_.size() != kAes256KeyLen ||
        rx_key_.size() != kAes256KeyLen ||
        tx_key_backup_.size() != kAes256KeyLen ||
        rx_key_backup_.size() != kAes256KeyLen ||
        key_integrity_hmac_key.empty()) {
        return false;
    }
    if (!ConstantTime::equals(tx_key_, tx_key_backup_) ||
        !ConstantTime::equals(rx_key_, rx_key_backup_)) {
        return false;
    }

    try {
        const std::array<uint8_t, 32> tx_computed =
            compute_hmac_sha256_or_throw(key_integrity_hmac_key, key_canary_input("tx", tx_key_));
        const std::array<uint8_t, 32> rx_computed =
            compute_hmac_sha256_or_throw(key_integrity_hmac_key, key_canary_input("rx", rx_key_));
        return std::equal(tx_computed.begin(), tx_computed.end(), tx_key_integrity_canary_.begin(), tx_key_integrity_canary_.end()) &&
               std::equal(rx_computed.begin(), rx_computed.end(), rx_key_integrity_canary_.begin(), rx_key_integrity_canary_.end());
    } catch (...) {
        return false;
    }
}

void SecureChannel::derive_directional_material_or_throw(const std::vector<uint8_t>& session_key) {
    tx_key_ = derive_bytes_or_throw(session_key, outbound_direction_label(role_) + ":key", kAes256KeyLen);
    rx_key_ = derive_bytes_or_throw(session_key, inbound_direction_label(role_) + ":key", kAes256KeyLen);

    const std::vector<uint8_t> tx_prefix = derive_bytes_or_throw(session_key, outbound_direction_label(role_) + ":iv", kPrefixLen);
    const std::vector<uint8_t> rx_prefix = derive_bytes_or_throw(session_key, inbound_direction_label(role_) + ":iv", kPrefixLen);
    std::copy(tx_prefix.begin(), tx_prefix.end(), tx_iv_prefix_.begin());
    std::copy(rx_prefix.begin(), rx_prefix.end(), rx_iv_prefix_.begin());

    tx_key_backup_ = tx_key_;
    rx_key_backup_ = rx_key_;

    if (secure_channel_debug_enabled() && logger) {
        logger->debug(
            "SecureChannel directional material role=" + role_name(role_) +
            " tx_prefix=" + bytes_to_hex_prefix(tx_iv_prefix_.data(), tx_iv_prefix_.size()) +
            " rx_prefix=" + bytes_to_hex_prefix(rx_iv_prefix_.data(), rx_iv_prefix_.size()));
    }
}

void SecureChannel::reset_replay_window() {
    replay_window_initialized = false;
    replay_window_top = 0;
    replay_window_bitmap = 0;
    recv_counter.store(-1, std::memory_order_relaxed);
}

bool SecureChannel::can_attempt_decrypt_for_counter(uint64_t counter) const {
    if (!replay_window_initialized) {
        return counter < kReplayWindowSize;
    }
    if (counter > replay_window_top) {
        return true;
    }

    const uint64_t delta = replay_window_top - counter;
    if (delta >= kReplayWindowSize) {
        return false;
    }

    const uint64_t mask = 1ULL << delta;
    return (replay_window_bitmap & mask) == 0;
}

uint64_t SecureChannel::parse_rx_counter_or_throw(const std::vector<uint8_t>& iv) const {
    if (iv.size() != kIvLen) {
        throw SecureChannelError("IV invalide (attendu 12 octets)");
    }
    for (size_t i = 0; i < kPrefixLen; ++i) {
        if (iv[i] != rx_iv_prefix_[i]) {
            if (secure_channel_debug_enabled() && logger) {
                logger->debug(
                    "SecureChannel rx prefix mismatch role=" + role_name(role_) +
                    " got=" + bytes_to_hex_prefix(iv.data(), kPrefixLen) +
                    " expected=" + bytes_to_hex_prefix(rx_iv_prefix_.data(), rx_iv_prefix_.size()));
            }
            throw ReplayDetected("IV prefix invalide pour ce sens de trafic");
        }
    }

    uint64_t counter = 0;
    for (int i = 0; i < static_cast<int>(kCounterLen); ++i) {
        counter = (counter << 8) | iv[kPrefixLen + i];
    }
    return counter;
}

bool SecureChannel::accept_replay_counter(uint64_t counter) {
    if (!replay_window_initialized) {
        if (counter >= kReplayWindowSize) {
            return false;
        }
        replay_window_initialized = true;
        replay_window_top = counter;
        replay_window_bitmap = 1ULL;
        recv_counter.store(static_cast<int64_t>(counter), std::memory_order_relaxed);
        return true;
    }

    if (counter > replay_window_top) {
        const uint64_t shift = counter - replay_window_top;
        replay_window_bitmap = (shift >= kReplayWindowSize)
            ? 1ULL
            : ((replay_window_bitmap << shift) | 1ULL);
        replay_window_top = counter;
        recv_counter.store(static_cast<int64_t>(counter), std::memory_order_relaxed);
        return true;
    }

    const uint64_t delta = replay_window_top - counter;
    if (delta >= kReplayWindowSize) {
        return false;
    }

    const uint64_t mask = 1ULL << delta;
    if ((replay_window_bitmap & mask) != 0) {
        return false;
    }

    replay_window_bitmap |= mask;
    return true;
}

void SecureChannel::secure_cleanup() {
    SecureMemory::zeroize(session_secret_);
    SecureMemory::zeroize(tx_key_);
    SecureMemory::zeroize(rx_key_);
    SecureMemory::zeroize(tx_key_backup_);
    SecureMemory::zeroize(rx_key_backup_);
    SecureMemory::zeroize(key_integrity_hmac_key);
    tx_key_integrity_canary_.fill(0);
    rx_key_integrity_canary_.fill(0);
    tx_iv_prefix_.fill(0);
    rx_iv_prefix_.fill(0);
    send_counter.store(0, std::memory_order_relaxed);
    key_epoch.store(0, std::memory_order_relaxed);
    reset_replay_window();
}

SecureChannel::SecureChannel(const std::vector<uint8_t>& session_key, SecureChannelRole role)
    : role_(role) {
    if (session_key.size() < kAes256KeyLen) {
        throw std::runtime_error("session_key trop courte pour AES-256");
    }

    session_secret_.assign(session_key.begin(), session_key.begin() + kAes256KeyLen);
    derive_directional_material_or_throw(session_secret_);
    send_counter.store(0, std::memory_order_relaxed);
    key_epoch.store(0, std::memory_order_relaxed);
    reset_replay_window();

    fault_detection_enabled = true;
    key_integrity_hmac_key = derive_bytes_or_throw(
        session_secret_,
        "hesia-secure-channel-integrity:v2",
        32);
    refresh_key_integrity_canary();

    logger = setup_logger("HESIA-SECURE-CHANNEL", Config::LOG_DIR);

    if (secure_channel_debug_enabled()) {
        logger->debug(
            "SecureChannel directional material role=" + role_name(role_) +
            " tx_prefix=" + bytes_to_hex_prefix(tx_iv_prefix_.data(), tx_iv_prefix_.size()) +
            " rx_prefix=" + bytes_to_hex_prefix(rx_iv_prefix_.data(), rx_iv_prefix_.size()));
    }

    RuntimeProtection::monitor_system_integrity();
    RuntimeProtection::randomize_execution();

#ifdef DEBUG
    logger->debug("SecureChannel initialized (DEBUG)");
#else
    logger->info("SecureChannel initialized with directional keys");
#endif
}

SecureChannel::~SecureChannel() {
    secure_cleanup();
}

EncryptedMessage SecureChannel::encrypt(const std::vector<uint8_t>& plaintext,
                                        const std::vector<uint8_t>& aad) {
    std::lock_guard<std::mutex> lock(state_mutex);

    if (plaintext.empty()) {
        throw SecureChannelError("Plaintext vide");
    }
    if (plaintext.size() > kMaxPayloadSize) {
        throw SecureChannelError("Plaintext trop volumineux");
    }

    RuntimeProtection::add_dummy_operations();

    if (!verify_key_integrity()) {
        throw SecureChannelError("Integrite de cle invalide");
    }

    const uint64_t current_counter = send_counter.load(std::memory_order_relaxed);
    if (current_counter > kMaxCounterPerEpoch) {
        throw SecureChannelError("Compteur de messages epuise pour cet epoch; rotation de cle requise");
    }
    send_counter.store(current_counter + 1, std::memory_order_relaxed);

    std::vector<uint8_t> iv = build_iv(tx_iv_prefix_, static_cast<uint32_t>(current_counter));
    std::vector<uint8_t> effective_aad = build_effective_aad(aad);

#ifdef DEBUG
    std::string iv_hex;
    for (uint8_t b : iv) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        iv_hex += buf;
    }
    logger->debug("[ENC] COUNTER=" + std::to_string(current_counter) +
                  ", IV=" + iv_hex +
                  ", Plaintext len=" + std::to_string(plaintext.size()));
#endif

    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;

#ifdef HESIA_USE_FIPS_MODULE
    auto& fm = hesia::fips::FipsModule::Instance();
    auto r = fm.Aes256GcmEncrypt(tx_key_, iv, effective_aad, plaintext);
    ciphertext = std::move(r.ciphertext);
    tag = std::move(r.tag);
    if (ciphertext.size() != plaintext.size() || tag.size() != kTagLen) {
        throw SecureChannelError("FIPS AES-256-GCM encrypt failed: " + fm.LastError());
    }
#else
    CacheProtection::enable_aes_ni_optimization();
    CacheProtection::flush_cache_lines();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, tx_key_.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    int len = 0;
    ciphertext.resize(plaintext.size());

    if (!effective_aad.empty()) {
        if (EVP_EncryptUpdate(ctx, nullptr, &len, effective_aad.data(),
                              static_cast<int>(effective_aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptUpdate AAD failed");
        }
    }

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    tag.resize(kTagLen);
    if (EVP_EncryptFinal_ex(ctx, nullptr, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CIPHER_CTX_ctrl failed");
    }

    EVP_CIPHER_CTX_free(ctx);
#endif

    if (fault_detection_enabled && !FaultDetection::verify_computation(plaintext, ciphertext)) {
        throw std::runtime_error("Detection de faute lors du chiffrement");
    }

    return EncryptedMessage(iv, ciphertext, tag);
}

std::vector<uint8_t> SecureChannel::decrypt(const std::vector<uint8_t>& iv,
                                            const std::vector<uint8_t>& ciphertext,
                                            const std::vector<uint8_t>& tag,
                                            const std::vector<uint8_t>& aad) {
    std::lock_guard<std::mutex> lock(state_mutex);

    if (tag.size() != kTagLen) {
        throw SecureChannelError("Tag GCM invalide");
    }
    if (ciphertext.size() > kMaxPayloadSize) {
        throw SecureChannelError("Ciphertext trop volumineux");
    }
    if (aad.size() > 1024) {
        throw SecureChannelError("AAD trop long");
    }
    if (!verify_key_integrity()) {
        throw SecureChannelError("Integrite de cle invalide");
    }

    const uint64_t counter = parse_rx_counter_or_throw(iv);
    // Pré-contrôle en lecture seule (ne modifie pas la fenêtre): rejette
    // rapidement les compteurs hors fenêtre ou déjà vus avant tout calcul.
    if (!can_attempt_decrypt_for_counter(counter)) {
        throw ReplayDetected("Message hors fenetre de rejeu ou deja vu");
    }

    if (secure_channel_debug_enabled() && logger) {
        logger->debug(
            "SecureChannel decrypt role=" + role_name(role_) +
            " iv_prefix=" + bytes_to_hex_prefix(iv.data(), kPrefixLen) +
            " expected_rx_prefix=" + bytes_to_hex_prefix(rx_iv_prefix_.data(), rx_iv_prefix_.size()) +
            " counter=" + std::to_string(counter));
    }

    // IMPORTANT: la fenêtre anti-rejeu n'est validée/consommée qu'APRÈS
    // l'authentification AEAD réussie (voir plus bas). Un message forgé avec un
    // compteur valide mais un tag GCM invalide ne doit jamais marquer ce
    // compteur comme « vu » (sinon DoS par empoisonnement de la fenêtre).
    std::vector<uint8_t> effective_aad = build_effective_aad(aad);
    std::vector<uint8_t> plaintext;

#ifdef HESIA_USE_FIPS_MODULE
    auto& fm = hesia::fips::FipsModule::Instance();
    plaintext = fm.Aes256GcmDecrypt(rx_key_, iv, effective_aad, ciphertext, tag);
    if (plaintext.empty() && !ciphertext.empty()) {
        throw InvalidTagError("Echec authentification GCM");
    }
#else
    CacheProtection::isolate_cache_access();
    PowerMasking::randomize_execution_timing();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        throw std::runtime_error("EVP_CIPHER_CTX_new failed: " + std::string(err_buf));
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, rx_key_.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    int len = 0;
    plaintext.resize(ciphertext.size());

    if (!effective_aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len, effective_aad.data(),
                              static_cast<int>(effective_aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptUpdate AAD failed");
        }
    }

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Echec authentification GCM");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                            const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Echec authentification GCM");
    }

    uint8_t final_buf[16];
    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx, final_buf, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Echec authentification GCM");
    }

    EVP_CIPHER_CTX_free(ctx);
#endif

    // Authentification AEAD réussie: on peut maintenant consommer le compteur
    // dans la fenêtre anti-rejeu. Le mutex state_mutex est détenu sur toute la
    // durée, donc l'état observé par can_attempt_decrypt_for_counter() n'a pas
    // changé; ce contrôle reste défensif (rejet d'une éventuelle réinsertion).
    if (!accept_replay_counter(counter)) {
        SecureMemory::zeroize(plaintext);
        throw ReplayDetected("Message hors fenetre de rejeu ou deja vu");
    }

    return plaintext;
}

void SecureChannel::enable_fault_detection(bool enable) {
    std::lock_guard<std::mutex> lock(state_mutex);
    fault_detection_enabled = enable;
    if (enable && (tx_key_backup_.empty() || rx_key_backup_.empty())) {
        tx_key_backup_ = tx_key_;
        rx_key_backup_ = rx_key_;
        refresh_key_integrity_canary();
    }
}

bool SecureChannel::is_fault_detection_enabled() const {
    return fault_detection_enabled;
}

void SecureChannel::rotate_key(const std::vector<uint8_t>& new_key) {
    std::lock_guard<std::mutex> lock(state_mutex);

    if (new_key.size() < kAes256KeyLen) {
        throw std::runtime_error("Nouvelle cle trop courte pour AES-256");
    }
    if (!verify_key_integrity()) {
        throw SecureChannelError("Refusing to rotate tampered secure channel key");
    }

    std::vector<uint8_t> hkdf_info;
    const std::string context_str = "hesia-secure-channel-key-rotation-v3";
    hkdf_info.insert(hkdf_info.end(), context_str.begin(), context_str.end());

    std::vector<uint8_t> next_session_secret =
        HKDF::derive(session_secret_, new_key, hkdf_info, kAes256KeyLen);

    SecureMemory::zeroize(session_secret_);
    SecureMemory::zeroize(tx_key_);
    SecureMemory::zeroize(rx_key_);
    SecureMemory::zeroize(tx_key_backup_);
    SecureMemory::zeroize(rx_key_backup_);

    session_secret_ = next_session_secret;
    key_epoch.fetch_add(1, std::memory_order_relaxed);
    derive_directional_material_or_throw(session_secret_);
    refresh_key_integrity_canary();

    send_counter.store(0, std::memory_order_relaxed);
    reset_replay_window();

    SecureMemory::zeroize(next_session_secret);
    SecureMemory::zeroize(hkdf_info);

    logger->info("SecureChannel key rotated with directional re-keying");
}

} // namespace hesia
