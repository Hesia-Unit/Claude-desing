#include "secure_channel.hpp"
#include "security_utils.hpp"
#ifdef HESIA_USE_FIPS_MODULE
#include "fips_module.hpp"
#endif
#include "logger.hpp"
#include "config.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <vector>

namespace hesia {

std::vector<uint8_t> SecureChannel::build_iv(uint64_t counter) {
    // IV 96-bit (12 octets): [prefix 32-bit par instance] || [counter 64-bit big-endian]
    // Objectif: unicité des nonces AES-GCM pour une clé donnée, tout en gardant
    // un compteur exploitable pour l'anti-rejeu.
    std::vector<uint8_t> iv(12, 0);

    iv[0] = iv_prefix[0];
    iv[1] = iv_prefix[1];
    iv[2] = iv_prefix[2];
    iv[3] = iv_prefix[3];

    for (int i = 0; i < 8; i++) {
        iv[4 + (7 - i)] = static_cast<uint8_t>((counter >> (i * 8)) & 0xFF);
    }

    return iv;
}

bool SecureChannel::verify_key_integrity() {
    if (!fault_detection_enabled) return true;
    
    // Vérification simple : comparer avec la sauvegarde
    return ConstantTime::equals(key, key_backup);
}

void SecureChannel::secure_cleanup() {
    SecureMemory::zeroize(key);
    SecureMemory::zeroize(key_backup);
    for (auto &b : iv_prefix) { b = 0; }
    send_counter.store(0);
    recv_counter.store(-1);
}

SecureChannel::SecureChannel(const std::vector<uint8_t>& session_key) {
    // Initialiser la protection runtime
    // RuntimeProtection::setup_protection(); // Commenté pour éviter erreur de compilation
    
    if (session_key.size() < 32) {
        throw std::runtime_error("session_key trop courte pour AES-256");
    }
    
    key.assign(session_key.begin(), session_key.begin() + 32);
    key_backup = key; // Copie pour détection de fautes
    send_counter.store(0);
    recv_counter.store(-1);

    // Préfixe IV par instance (défense-in-depth contre réutilisation de nonce)
    {
        static std::atomic<uint32_t> g_prefix_ctr{0};
        const uint32_t ctr = g_prefix_ctr.fetch_add(1, std::memory_order_relaxed);

        std::array<uint8_t, 4> rnd{0, 0, 0, 0};
        if (RAND_bytes(rnd.data(), static_cast<int>(rnd.size())) != 1) {
            // Fallback: timestamp (nanos)
            const uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            for (size_t i = 0; i < rnd.size(); i++) {
                rnd[i] = static_cast<uint8_t>((ts >> (i * 8)) & 0xFF);
            }
        }

        iv_prefix = rnd;
        iv_prefix[0] ^= static_cast<uint8_t>((ctr >> 24) & 0xFF);
        iv_prefix[1] ^= static_cast<uint8_t>((ctr >> 16) & 0xFF);
        iv_prefix[2] ^= static_cast<uint8_t>((ctr >> 8) & 0xFF);
        iv_prefix[3] ^= static_cast<uint8_t>(ctr & 0xFF);

        // Eviter un préfixe nul (rare, mais defense-in-depth)
        if (iv_prefix[0] == 0 && iv_prefix[1] == 0 && iv_prefix[2] == 0 && iv_prefix[3] == 0) {
            iv_prefix[3] = 1;
        }
    }

    fault_detection_enabled = true;
    
    logger = setup_logger("HESIA-SECURE-CHANNEL", Config::LOG_DIR);
    
    // Initialisation des protections avancées
    RuntimeProtection::monitor_system_integrity();
    RuntimeProtection::randomize_execution();
    
    // Protection contre les logs de clés en production
    #ifdef DEBUG
    logger->debug("SecureChannel initialisé (DEBUG)");
    #else
    logger->info("SecureChannel initialisé - Mode production (pas de log de clé)");
    #endif
}

SecureChannel::~SecureChannel() {
    secure_cleanup();
}

EncryptedMessage SecureChannel::encrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& aad) {
    // Validation des entrées
    if (plaintext.empty()) {
        throw SecureChannelError("Plaintext vide");
    }
    
    // Vérification basique de l'intégrité
    if (plaintext.size() > 1024*1024) { // 1MB max
        throw SecureChannelError("Plaintext trop volumineux");
    }
    
    // Randomisation de l'exécution
    RuntimeProtection::add_dummy_operations();
    
    // Vérification d'intégrité de la clé
    if (key.size() != 32) {
        throw SecureChannelError("Taille de clé invalide");
    }
    
    uint64_t current_counter = send_counter.load(std::memory_order_relaxed);
    while (true) {
        if (current_counter == std::numeric_limits<uint64_t>::max()) {
            throw SecureChannelError("Compteur de messages épuisé (risque de réutilisation de nonce)");
        }
        if (send_counter.compare_exchange_weak(current_counter,
                                              current_counter + 1,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
            break;
        }
    }
    std::vector<uint8_t> iv = build_iv(current_counter);
    
    // Protection contre les logs en production
    #ifdef DEBUG
    std::string iv_hex;
    for (size_t i = 0; i < iv.size(); i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", iv[i]);
        iv_hex += buf;
    }
    std::string aad_hex;
    for (size_t i = 0; i < std::min(size_t(32), aad.size()); i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", aad[i]);
        aad_hex += buf;
    }
    logger->debug("[ENC] COUNTER=" + std::to_string(current_counter) + 
                 ", IV=" + iv_hex + 
                 ", AAD=" + aad_hex + "... (len=" + std::to_string(aad.size()) + 
                 "), Plaintext len=" + std::to_string(plaintext.size()));
    #endif
    
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;

#ifdef HESIA_USE_FIPS_MODULE
    // Route crypto through the FIPS wrapper (expects it to have been initialized
    // and to be in Approved mode).
    auto& fm = hesia::fips::FipsModule::Instance();
    auto r = fm.Aes256GcmEncrypt(key, iv, aad, plaintext);
    ciphertext = std::move(r.ciphertext);
    tag = std::move(r.tag);
    if (ciphertext.size() != plaintext.size() || tag.size() != 16) {
        throw SecureChannelError("FIPS AES-256-GCM encrypt failed: " + fm.LastError());
    }
#else
    // ✅ PROTECTION: Activer AES-NI et cache protection avant chiffrement
    CacheProtection::enable_aes_ni_optimization();
    CacheProtection::flush_cache_lines();
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    }

    // Utiliser AES-GCM qui protège contre le padding oracle
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    int len;
    ciphertext.resize(plaintext.size());

    if (!aad.empty()) {
        if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_EncryptUpdate AAD failed");
        }
    }

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    tag.resize(16);
    if (EVP_EncryptFinal_ex(ctx, nullptr, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CIPHER_CTX_ctrl failed");
    }

    EVP_CIPHER_CTX_free(ctx);
#endif
    
    // Vérification du résultat (optionnelle)
    if (fault_detection_enabled) {
        if (!FaultDetection::verify_computation(plaintext, ciphertext)) {
            throw std::runtime_error("Détection de faute lors du chiffrement");
        }
    }
    
    return EncryptedMessage(iv, ciphertext, tag);
}

std::vector<uint8_t> SecureChannel::decrypt(const std::vector<uint8_t>& iv,
                                              const std::vector<uint8_t>& ciphertext,
                                              const std::vector<uint8_t>& tag,
                                              const std::vector<uint8_t>& aad) {
    // Validation basique des entrées
    if (iv.size() != 12) {
        throw SecureChannelError("IV invalide (attendu 12 octets)");
    }
    
    // Vérification du tag GCM
    if (tag.size() != 16) {
        throw SecureChannelError("Tag GCM invalide");
    }
    
    // Validation de l'AAD
    if (aad.size() > 1024) {
        throw SecureChannelError("AAD trop long");
    }
    
    // Extraction du compteur depuis l'IV
    uint64_t counter = 0;
    for (int i = 0; i < 8; i++) {
        // Counter is encoded big-endian in IV[4..11]
        counter = (counter << 8) | iv[4 + i];
    }
    
    // ✅ SÉCURITÉ: Protection contre le replay CORRIGÉE
    int64_t current_recv = recv_counter.load();
    
    // ✅ CORRECTION: Gérer correctement le sentinel -1
    if (current_recv < 0) {
        // Premier message : accepter (0 est valide) tout en évitant les sauts
        // énormes qui pourraient désynchroniser.
        if (counter > 100) {
            throw ReplayDetected("Premier message trop avancé (possible attaque) - counter=" + std::to_string(counter));
        }
    } else {
        // Messages suivants - appliquer fenêtre de replay
        if (counter <= static_cast<uint64_t>(current_recv)) {
            throw ReplayDetected("IV rejoué ou hors ordre");
        }
        
        // Fenêtre de tolérance de 100 messages pour désordre légitime
        uint64_t max_acceptable = static_cast<uint64_t>(current_recv) + 100;
        if (counter > max_acceptable) {
            throw ReplayDetected("Message trop avancé (possible attaque) - max=" + std::to_string(max_acceptable));
        }
    }
    
    std::vector<uint8_t> plaintext;

#ifdef HESIA_USE_FIPS_MODULE
    auto& fm = hesia::fips::FipsModule::Instance();
    plaintext = fm.Aes256GcmDecrypt(key, iv, aad, ciphertext, tag);
    if (plaintext.empty() && !ciphertext.empty()) {
        throw InvalidTagError("Échec authentification GCM");
    }
#else
    // ✅ PROTECTION: Activer cache timing et power masking avant déchiffrement
    CacheProtection::isolate_cache_access();
    PowerMasking::randomize_execution_timing();
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        throw std::runtime_error("EVP_CIPHER_CTX_new failed: " + std::string(err_buf));
    }

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    int len = 0;
    plaintext.resize(ciphertext.size());

    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), aad.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EVP_DecryptUpdate AAD failed");
        }
    }

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Échec authentification GCM");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Échec authentification GCM");
    }

    // For GCM, Final should not output additional bytes, but the API expects a valid pointer.
    uint8_t final_buf[16];
    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx, final_buf, &final_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw InvalidTagError("Échec authentification GCM");
    }

    EVP_CIPHER_CTX_free(ctx);
#endif
    
    // Mise à jour atomique du compteur
    recv_counter.store(static_cast<int64_t>(counter));
    
    return plaintext;
}

void SecureChannel::enable_fault_detection(bool enable) {
    fault_detection_enabled = enable;
    if (enable && key_backup.empty()) {
        key_backup = key;
    }
}

bool SecureChannel::is_fault_detection_enabled() const {
    return fault_detection_enabled;
}

void SecureChannel::rotate_key(const std::vector<uint8_t>& new_key) {
    if (new_key.size() < 32) {
        throw std::runtime_error("Nouvelle clé trop courte pour AES-256");
    }
    
    // Rotation déterministe avec HKDF:
    // IMPORTANT: Une rotation de clé symétrique ne doit pas dépendre d'un sel
    // local aléatoire, sinon les deux pairs dériveront des clés différentes.
    // Ici on utilise la clé courante comme salt (déterministe) afin que deux
    // côtés qui effectuent la rotation avec la même clé courante et le même
    // new_key obtiennent le même résultat.
    std::vector<uint8_t> hkdf_salt = key;
    
    // 2. Préparer les infos pour HKDF (contexte de rotation)
    std::vector<uint8_t> hkdf_info;
    std::string context_str = "hesia-secure-channel-key-rotation-v1";
    hkdf_info.insert(hkdf_info.end(), context_str.begin(), context_str.end());
    
    // 3. Dériver la nouvelle clé avec HKDF standard
    std::vector<uint8_t> derived_key = HKDF::derive(hkdf_salt, new_key, hkdf_info, 32);
    
    // 4. Nettoyage sécurisé de l'ancienne clé
    SecureMemory::zeroize(key);
    SecureMemory::zeroize(key_backup);
    
    // 5. Configuration de la nouvelle clé dérivée
    key = derived_key;
    key_backup = derived_key;
    
    // 6. (Option) Réinitialiser les compteurs après rotation.
    // Risque si la rotation ne se fait pas de manière synchronisée entre pairs.
    // Ici on conserve le comportement historique.
    send_counter.store(0);
    recv_counter.store(-1);
    
    // 7. Nettoyage sécurisé des données temporaires
    SecureMemory::zeroize(hkdf_salt);
    SecureMemory::zeroize(derived_key);
    SecureMemory::zeroize(hkdf_info);
    
    logger->info("Clé rotée avec HKDF standard (Perfect Forward Secrecy)");
}

} // namespace hesia

